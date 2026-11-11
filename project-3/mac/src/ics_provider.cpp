#include "ics_provider.hpp"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "pairing.hpp"
#include "rma3/ics.hpp"

namespace rma {
namespace {

constexpr size_t kMaxBytes = 20 * 1024 * 1024;

double wall_now() {
  using namespace std::chrono;
  return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

bool starts_with_nocase(const std::string& s, const char* prefix) {
  size_t i = 0;
  for (; prefix[i]; ++i) {
    if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
  }
  return true;
}

}  // namespace

IcsProvider::IcsProvider() : link_(load_ics_link()) {}

IcsProvider::~IcsProvider() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

const char* IcsProvider::name() const { return "iCal link (.ics)"; }

const char* IcsProvider::help() const {
  return "Reads a calendar's published iCal (.ics) link, downloaded every 5 minutes, or a .ics file. "
         "Outlook on the web: Settings > Calendar > Shared calendars > Publish a calendar > ICS. "
         "Google: the calendar's settings > Secret address in iCal format. Anyone with the link can read "
         "the calendar, so it is kept only in your user folder on this computer.";
}

CalendarProvider::Access IcsProvider::access() const {
  return link_.empty() ? Access::Denied : access_.load();
}

std::string IcsProvider::denied_help() const {
  if (link_.empty()) return "Paste the calendar's iCal link in Settings > Calendar source.";
  std::lock_guard<std::mutex> lock(mu_);
  return "Couldn't read the iCal link (" + error_ + "). Check it in Settings > Calendar source.";
}

std::string IcsProvider::problem() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::string out;
  if (!error_.empty() && fetched_at_ > 0) {
    long mins = static_cast<long>((wall_now() - fetched_at_) / 60);
    out = "Last download failed (" + error_ + "); using the copy from " + std::to_string(mins) + " min ago.";
  }
  if (unsupported_ > 0) {
    if (!out.empty()) out += " ";
    out += std::to_string(unsupported_) +
           " repeating event(s) use a rule this app can't expand; only their first time is used.";
  }
  return out;
}

void IcsProvider::request_access() {
  if (link_.empty() || worker_.joinable()) return;
  access_ = Access::Requesting;
  worker_ = std::thread([this] { run(); });
}

bool IcsProvider::load(std::string* text, std::string* error) const {
  std::string url = link_;
  if (starts_with_nocase(url, "webcal://")) url = "https://" + url.substr(9);
  if (starts_with_nocase(url, "http://")) {
    *error = "the link must start with https://";
    return false;
  }
  if (starts_with_nocase(url, "https://")) return fetch_url(url, text, error);
  // Otherwise a file on this computer (e.g. an exported .ics).
  std::ifstream f(std::filesystem::u8path(url), std::ios::binary);
  if (!f) {
    *error = "can't open that file";
    return false;
  }
  text->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  if (text->size() > kMaxBytes) {
    *error = "the file is too big";
    return false;
  }
  return true;
}

void IcsProvider::run() {
  while (!stop_) {
    std::string text, error;
    bool ok = load(&text, &error);
    rma3::IcsResult parsed;
    if (ok) {
      parsed = rma3::parse_ics(text, wall_now() - 86400, wall_now() + 86400);
      if (!parsed.ok) {
        ok = false;
        error = "that isn't an iCal calendar";
      }
    }
    int wait_s = ok ? kEveryS : kRetryS;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (ok) {
        bool same = text == text_;
        text_.swap(text);
        error_.clear();
        name_ = parsed.name;
        fetched_at_ = wall_now();
        if (!same) changed_ = true;
      } else {
        error_ = error;
      }
      if (ok) access_ = Access::Granted;
      else if (fetched_at_ == 0) access_ = Access::Denied;  // never worked yet
    }
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::seconds(wait_s), [this] { return stop_.load(); });
  }
}

std::vector<CalendarInfo> IcsProvider::calendars() {
  std::vector<CalendarInfo> out;
  if (access() != Access::Granted) return out;
  CalendarInfo info;
  info.id = "ics";
  std::lock_guard<std::mutex> lock(mu_);
  info.title = name_.empty() ? "Published calendar" : name_;
  info.account = "iCal link";
  info.rgb[0] = 0.35f;
  info.rgb[1] = 0.55f;
  info.rgb[2] = 0.9f;
  out.push_back(info);
  return out;
}

std::vector<rma3::CalEvent> IcsProvider::events(const std::vector<std::string>& calendar_ids, double from,
                                                double to) {
  bool wanted = false;
  for (const auto& id : calendar_ids) wanted = wanted || id == "ics";
  if (!wanted) return {};
  std::lock_guard<std::mutex> lock(mu_);
  if (text_.empty()) return {};
  rma3::IcsResult r = rma3::parse_ics(text_, from, to);
  unsupported_ = r.unsupported;
  return r.events;
}

}  // namespace rma
