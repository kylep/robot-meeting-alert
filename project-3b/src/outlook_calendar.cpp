// The Windows app's calendar sources (calendar_provider.hpp):
//   kOutlook   classic Outlook for Windows, through its COM automation
//              interface (the "Outlook object model"). Reads the calendars
//              Outlook already has, so it works for Exchange / Microsoft 365,
//              Outlook.com and local PST calendars with no sign-in of its own.
//              The "new Outlook for Windows" has no such interface; its users
//              need kIcsLink.
//   kIcsLink   the shared iCal link source (../project-3/mac/src/ics_provider.*).
//
// Only these appointment properties are read: Start, End, AllDayEvent,
// BusyStatus, MeetingStatus, ResponseStatus, EntryID, Class. None of them is
// behind Outlook's security prompt (no Subject, Body, Organizer, Recipients),
// and only start/end times ever leave this file.
//
// All COM calls happen on one worker thread (its own apartment); the UI only
// reads cached results, so a slow or busy Outlook never freezes the window.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "calendar_provider.hpp"
#include "ics_provider.hpp"

namespace rma {
namespace {

// Outlook object model constants (OlDefaultFolders, OlItemType, ...).
constexpr long kFolderCalendar = 9;       // olFolderCalendar
constexpr long kAppointmentItem = 1;      // olAppointmentItem (Folder.DefaultItemType)
constexpr long kClassAppointment = 26;    // olAppointment (item.Class)
constexpr long kBusyFree = 0;             // olFree
constexpr long kMeetingCanceled = 5;      // olMeetingCanceled
constexpr long kMeetingReceivedCanceled = 7;  // olMeetingReceivedAndCanceled
constexpr long kResponseDeclined = 4;     // olResponseDeclined
constexpr int kMaxItemsPerFolder = 1000;  // repeating items without an end are endless

// ---------------------------------------------------------------- COM glue

std::string utf8(const wchar_t* w, int len = -1) {
  if (!w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
  if (len < 0 && !s.empty() && s.back() == '\0') s.pop_back();
  return s;
}

std::wstring wide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

struct Var {
  VARIANT v;
  Var() { VariantInit(&v); }
  ~Var() { VariantClear(&v); }
  Var(const Var&) = delete;
  Var& operator=(const Var&) = delete;
};

// An owned IDispatch reference.
class Disp {
 public:
  Disp() = default;
  explicit Disp(IDispatch* p) : p_(p) {}
  Disp(Disp&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  Disp& operator=(Disp&& o) noexcept {
    if (this != &o) {
      reset();
      p_ = o.p_;
      o.p_ = nullptr;
    }
    return *this;
  }
  Disp(const Disp&) = delete;
  Disp& operator=(const Disp&) = delete;
  ~Disp() { reset(); }
  void reset() {
    if (p_) p_->Release();
    p_ = nullptr;
  }
  IDispatch* get() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }

 private:
  IDispatch* p_ = nullptr;
};

// Arguments are given in natural order; VARIANTs are borrowed, not freed.
HRESULT invoke(IDispatch* d, WORD flags, const wchar_t* name, VARIANT* result, std::vector<VARIANT> args = {}) {
  if (!d) return E_POINTER;
  DISPID id = 0;
  LPOLESTR n = const_cast<LPOLESTR>(name);
  HRESULT hr = d->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
  if (FAILED(hr)) return hr;
  std::reverse(args.begin(), args.end());  // IDispatch wants them last-first
  DISPPARAMS dp{args.empty() ? nullptr : args.data(), nullptr, static_cast<UINT>(args.size()), 0};
  DISPID put = DISPID_PROPERTYPUT;
  if (flags & DISPATCH_PROPERTYPUT) {
    dp.rgdispidNamedArgs = &put;
    dp.cNamedArgs = 1;
  }
  return d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, nullptr, nullptr);
}

HRESULT get(IDispatch* d, const wchar_t* name, Var* out, std::vector<VARIANT> args = {}) {
  return invoke(d, DISPATCH_PROPERTYGET | DISPATCH_METHOD, name, &out->v, std::move(args));
}

// A property or method returning an object; empty Disp (and *hr set) if not.
Disp get_obj(IDispatch* d, const wchar_t* name, HRESULT* hr, std::vector<VARIANT> args = {}) {
  Var r;
  *hr = get(d, name, &r, std::move(args));
  if (FAILED(*hr)) return Disp();
  if (r.v.vt != VT_DISPATCH || r.v.pdispVal == nullptr) return Disp();
  Disp out(r.v.pdispVal);
  r.v.vt = VT_EMPTY;  // ownership moved
  return out;
}

bool get_long(IDispatch* d, const wchar_t* name, long* out) {
  Var r;
  if (FAILED(get(d, name, &r)) || FAILED(VariantChangeType(&r.v, &r.v, 0, VT_I4))) return false;
  *out = r.v.lVal;
  return true;
}

bool get_string(IDispatch* d, const wchar_t* name, std::string* out) {
  Var r;
  if (FAILED(get(d, name, &r)) || FAILED(VariantChangeType(&r.v, &r.v, 0, VT_BSTR))) return false;
  *out = utf8(r.v.bstrVal, static_cast<int>(SysStringLen(r.v.bstrVal)));
  return true;
}

// An Outlook date (local time) to seconds since 1970.
bool get_time(IDispatch* d, const wchar_t* name, double* out) {
  Var r;
  if (FAILED(get(d, name, &r)) || r.v.vt != VT_DATE) return false;
  SYSTEMTIME local{}, utc{};
  FILETIME ft{};
  if (!VariantTimeToSystemTime(r.v.date, &local) || !TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) ||
      !SystemTimeToFileTime(&utc, &ft)) {
    return false;
  }
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  *out = static_cast<double>((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
  return true;
}

// Seconds since 1970 as Outlook's filter wants it: local time, in the
// user's short date and time formats, without seconds.
std::wstring filter_time(double t) {
  ULARGE_INTEGER u;
  u.QuadPart = static_cast<ULONGLONG>(t) * 10000000ULL + 116444736000000000ULL;
  FILETIME ft{u.LowPart, u.HighPart};
  SYSTEMTIME utc{}, local{};
  FileTimeToSystemTime(&ft, &utc);
  SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
  wchar_t date[80] = {}, time[80] = {};
  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 80, nullptr);
  GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 80);
  return std::wstring(date) + L" " + time;
}

class Bstr {
 public:
  explicit Bstr(const std::wstring& s) : b_(SysAllocStringLen(s.data(), static_cast<UINT>(s.size()))) {}
  ~Bstr() { SysFreeString(b_); }
  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
  VARIANT var() const {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = b_;
    return v;
  }

 private:
  BSTR b_;
};

VARIANT vlong(long x) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = x;
  return v;
}

VARIANT vbool(bool x) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_BOOL;
  v.boolVal = x ? VARIANT_TRUE : VARIANT_FALSE;
  return v;
}

// Errors meaning Outlook quit, crashed or is too busy to answer right now.
bool outlook_gone(HRESULT hr) {
  return hr == RPC_E_DISCONNECTED || hr == RPC_E_CALL_REJECTED || hr == RPC_E_SERVERCALL_RETRYLATER ||
         hr == CO_E_OBJNOTCONNECTED || hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) ||
         hr == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED);
}

std::string hex(HRESULT hr) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
  return buf;
}

// ---------------------------------------------------------------- provider

class OutlookCalendar : public CalendarProvider {
 public:
  ~OutlookCalendar() override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  const char* name() const override { return "Outlook (classic)"; }
  const char* help() const override {
    return "Reads the calendars in classic Outlook for Windows (Microsoft 365, Exchange, Outlook.com, or a "
           "local file) while Outlook is installed on this PC. The \"new Outlook\" can't be read by other apps: "
           "use the iCal link source instead.";
  }
  Access access() const override { return access_.load(); }
  std::string denied_help() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
  }
  std::string problem() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return access_ == Access::Granted ? error_ : std::string();
  }

  void request_access() override {
    if (worker_.joinable()) return;
    access_ = Access::Requesting;
    worker_ = std::thread([this] { run(); });
  }

  std::vector<CalendarInfo> calendars() override {
    std::lock_guard<std::mutex> lock(mu_);
    return calendars_;
  }

  std::vector<rma3::CalEvent> events(const std::vector<std::string>& ids, double from, double to) override {
    std::lock_guard<std::mutex> lock(mu_);
    bool new_ids = ids != want_ids_;
    want_ids_ = ids;
    want_from_ = from;
    want_to_ = to;
    if (new_ids) {
      events_.clear();  // so whatever the worker finds counts as a change
      wake_ = true;
      cv_.notify_all();
      return {};  // the worker fetches the new choice right away
    }
    return events_;
  }

  bool take_changed() override { return changed_.exchange(false); }
  void refresh() override {}  // Outlook keeps its own copy in sync

 private:
  static constexpr auto kEvery = std::chrono::seconds(30);
  static constexpr auto kListEvery = std::chrono::minutes(5);

  void set_error(const std::string& e) {
    std::lock_guard<std::mutex> lock(mu_);
    error_ = e;
  }

  // Outlook.Application -> Namespace("MAPI"). Fills error_ on failure.
  bool connect() {
    CLSID clsid;
    HRESULT hr = CLSIDFromProgID(L"Outlook.Application", &clsid);
    if (FAILED(hr)) {
      set_error(
          "Classic Outlook isn't installed on this PC (the \"new Outlook for Windows\" can't be read by other "
          "apps). Choose \"iCal link (.ics)\" in Settings instead.");
      return false;
    }
    IDispatch* raw = nullptr;
    // Joins the running Outlook, or starts it in the background.
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&raw));
    if (FAILED(hr) || !raw) {
      set_error(hr == CO_E_SERVER_EXEC_FAILURE
                    ? "Couldn't reach Outlook. If Outlook or this app runs \"as administrator\", start both normally. "
                      "Trying again every 30 seconds."
                    : "Couldn't start Outlook (" + hex(hr) + "). Check that Outlook opens normally. Trying again "
                      "every 30 seconds.");
      return false;
    }
    app_ = Disp(raw);
    Bstr mapi(L"MAPI");
    ns_ = get_obj(app_.get(), L"GetNamespace", &hr, {mapi.var()});
    if (!ns_) {
      app_.reset();
      set_error("Outlook didn't open its mail profile (" + hex(hr) + "). Open Outlook once and sign in.");
      return false;
    }
    // Uses the default profile if Outlook wasn't running; harmless if it was.
    Bstr empty(L"");
    Var ignored;
    invoke(ns_.get(), DISPATCH_METHOD, L"Logon", &ignored.v, {empty.var(), empty.var(), vbool(false), vbool(false)});
    return true;
  }

  void add_folder(IDispatch* folder, const std::string& account, const std::string& store_id, int depth,
                  std::vector<CalendarInfo>* out) {
    CalendarInfo info;
    std::string entry;
    if (!get_string(folder, L"EntryID", &entry) || !get_string(folder, L"Name", &info.title)) return;
    info.id = entry + "|" + store_id;
    info.account = account;
    info.rgb[0] = 0.2f;
    info.rgb[1] = 0.45f;
    info.rgb[2] = 0.85f;
    out->push_back(info);
    if (depth >= 3) return;
    HRESULT hr;
    Disp subs = get_obj(folder, L"Folders", &hr);
    long n = 0;
    if (!subs || !get_long(subs.get(), L"Count", &n)) return;
    for (long i = 1; i <= n; ++i) {
      Disp sub = get_obj(subs.get(), L"Item", &hr, {vlong(i)});
      long type = -1;
      if (sub && get_long(sub.get(), L"DefaultItemType", &type) && type == kAppointmentItem) {
        add_folder(sub.get(), account, store_id, depth + 1, out);
      }
    }
  }

  // Every account's calendar and its calendar subfolders. false = Outlook gone.
  bool list_calendars(std::vector<CalendarInfo>* out) {
    HRESULT hr;
    Disp stores = get_obj(ns_.get(), L"Stores", &hr);
    long n = 0;
    if (!stores || !get_long(stores.get(), L"Count", &n)) return false;
    for (long i = 1; i <= n; ++i) {
      Disp store = get_obj(stores.get(), L"Item", &hr, {vlong(i)});
      std::string account, store_id;
      if (!store || !get_string(store.get(), L"DisplayName", &account) ||
          !get_string(store.get(), L"StoreID", &store_id)) {
        continue;
      }
      Disp cal = get_obj(store.get(), L"GetDefaultFolder", &hr, {vlong(kFolderCalendar)});
      if (cal) add_folder(cal.get(), account, store_id, 0, out);  // archives etc. have none
    }
    std::sort(out->begin(), out->end(), [](const CalendarInfo& a, const CalendarInfo& b) {
      return a.account != b.account ? a.account < b.account : a.title < b.title;
    });
    return true;
  }

  // Occurrences overlapping [from, to) in one calendar. false = Outlook gone.
  bool folder_events(const std::string& id, double from, double to, std::vector<rma3::CalEvent>* out) {
    size_t bar = id.find('|');
    if (bar == std::string::npos) return true;
    Bstr entry(wide(id.substr(0, bar))), store(wide(id.substr(bar + 1)));
    HRESULT hr;
    Disp folder = get_obj(ns_.get(), L"GetFolderFromID", &hr, {entry.var(), store.var()});
    if (!folder) return !outlook_gone(hr);  // a calendar deleted since it was ticked: skip it
    Disp items = get_obj(folder.get(), L"Items", &hr);
    if (!items) return false;
    // Microsoft's documented order: sort by Start, include repeats, then filter.
    Bstr by_start(L"[Start]");
    Var ignored;
    if (FAILED(invoke(items.get(), DISPATCH_METHOD, L"Sort", &ignored.v, {by_start.var()}))) return false;
    Var ignored2;
    if (FAILED(invoke(items.get(), DISPATCH_PROPERTYPUT, L"IncludeRecurrences", &ignored2.v, {vbool(true)}))) {
      return false;
    }
    std::wstring filter = L"[Start] < '" + filter_time(to + 60) + L"' AND [End] > '" + filter_time(from - 60) + L"'";
    Bstr f(filter);
    Disp found = get_obj(items.get(), L"Restrict", &hr, {f.var()});
    if (!found) return false;
    Disp item = get_obj(found.get(), L"GetFirst", &hr);
    for (int i = 0; item && i < kMaxItemsPerFolder; ++i) {
      long cls = 0;
      rma3::CalEvent e;
      if (get_long(item.get(), L"Class", &cls) && cls == kClassAppointment &&
          get_time(item.get(), L"Start", &e.start) && get_time(item.get(), L"End", &e.end)) {
        if (e.start >= to) break;  // sorted by start: the rest are later
        long all_day = 0, busy = 2, meeting = 0, response = 0;
        get_long(item.get(), L"AllDayEvent", &all_day);
        get_long(item.get(), L"BusyStatus", &busy);
        get_long(item.get(), L"MeetingStatus", &meeting);
        get_long(item.get(), L"ResponseStatus", &response);
        e.all_day = all_day != 0;
        e.free = busy == kBusyFree;
        e.cancelled = meeting == kMeetingCanceled || meeting == kMeetingReceivedCanceled;
        e.declined = response == kResponseDeclined;
        get_string(item.get(), L"EntryID", &e.id);  // never sent; mixed with the start time
        if (e.end > from) out->push_back(e);
      }
      item = get_obj(found.get(), L"GetNext", &hr);
    }
    return true;
  }

  void run() {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto last_list = std::chrono::steady_clock::time_point{};
    bool listed = false;
    while (!stop_) {
      bool ok = ns_ || connect();
      if (ok) {
        std::vector<CalendarInfo> cals;
        auto now = std::chrono::steady_clock::now();
        bool relist = !listed || now - last_list >= kListEvery;
        if (relist) ok = list_calendars(&cals);
        std::vector<std::string> ids;
        double from = 0, to = 0;
        {
          std::lock_guard<std::mutex> lock(mu_);
          ids = want_ids_;
          from = want_from_;
          to = want_to_ + 3600;  // slack until the next round
          wake_ = false;
        }
        std::vector<rma3::CalEvent> evs;
        for (size_t i = 0; ok && i < ids.size() && to > from; ++i) ok = folder_events(ids[i], from, to, &evs);
        if (ok) {
          std::lock_guard<std::mutex> lock(mu_);
          if (relist) {
            listed = true;
            last_list = now;
            if (cals.size() != calendars_.size() ||
                !std::equal(cals.begin(), cals.end(), calendars_.begin(),
                            [](const CalendarInfo& a, const CalendarInfo& b) { return a.id == b.id && a.title == b.title; })) {
              calendars_ = cals;
              changed_ = true;
            }
          }
          bool same = evs.size() == events_.size() &&
                      std::equal(evs.begin(), evs.end(), events_.begin(), [](const rma3::CalEvent& a, const rma3::CalEvent& b) {
                        return a.start == b.start && a.end == b.end && a.free == b.free && a.declined == b.declined &&
                               a.cancelled == b.cancelled && a.all_day == b.all_day;
                      });
          if (!same) {
            events_ = evs;
            changed_ = true;
          }
          error_.clear();
          access_ = Access::Granted;
        } else {
          // Outlook was closed or stopped answering: start over next round,
          // keeping the last calendar so the robot carries on meanwhile.
          ns_.reset();
          app_.reset();
          set_error("Outlook isn't answering; trying again every 30 seconds.");
        }
      }
      if (!ok && access_ != Access::Granted) {
        access_ = Access::Denied;
        changed_ = true;
      }
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, kEvery, [this] { return stop_ || wake_; });
    }
    ns_.reset();
    app_.reset();
    if (SUCCEEDED(init)) CoUninitialize();
  }

  std::atomic<Access> access_{Access::Unknown};
  std::atomic<bool> changed_{false};
  std::atomic<bool> stop_{false};
  bool wake_ = false;  // guarded by mu_
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::string error_;
  std::vector<CalendarInfo> calendars_;
  std::vector<rma3::CalEvent> events_;
  std::vector<std::string> want_ids_;
  double want_from_ = 0, want_to_ = 0;
  Disp app_, ns_;  // worker thread only
  std::thread worker_;
};

}  // namespace

const char* calendar_kind_name(CalendarKind kind) {
  return kind == CalendarKind::kIcsLink ? "iCal link (.ics)" : "Outlook (classic)";
}

std::unique_ptr<CalendarProvider> make_calendar_provider(CalendarKind kind) {
  if (kind == CalendarKind::kIcsLink) return std::make_unique<IcsProvider>();
  return std::make_unique<OutlookCalendar>();
}

}  // namespace rma
