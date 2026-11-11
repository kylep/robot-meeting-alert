// Where the app gets meetings from. Only times leave a provider (see
// rma3::CalEvent); titles and people are never read out.
//
//   kMacCalendar  EventKit: every account added to the Mac's Calendar app
//                 (System Settings > Internet Accounts). Built, the default.
//   kGoogleApi    Sign in with Google and read the Calendar API directly.
//                 Stub: for a work laptop whose Calendar app can't add the
//                 Workspace account. See docs/work-calendar.md.
//   kIcsLink      A published calendar's iCal (.ics) link, fetched over
//                 HTTPS, or an .ics file (ics_provider.hpp). Built.
// The Windows app (../project-3b) has its own list: classic Outlook, then
// the same iCal link source.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rma3/host.hpp"

namespace rma {

#ifdef _WIN32
enum class CalendarKind { kOutlook = 0, kIcsLink = 1 };
constexpr int kCalendarKinds = 2;
#else
enum class CalendarKind { kMacCalendar = 0, kGoogleApi = 1, kIcsLink = 2 };
constexpr int kCalendarKinds = 3;
#endif

struct CalendarInfo {
  std::string id;       // the provider's calendar identifier
  std::string title;    // shown in the app's picker only
  std::string account;  // e.g. "Google" / the account's name
  float rgb[3] = {0.5f, 0.5f, 0.5f};
};

class CalendarProvider {
 public:
  enum class Access { Unknown, Requesting, Granted, Denied, NotBuilt };

  virtual ~CalendarProvider() = default;
  virtual const char* name() const = 0;
  // One or two sentences for the Settings tab: what this option needs.
  virtual const char* help() const = 0;
  virtual Access access() const = 0;
  // What to do when access() is Denied.
  virtual std::string denied_help() const {
    return "Calendar access is off. Turn it on in System Settings > Privacy & Security > Calendars, "
           "then reopen the app.";
  }
  // A current problem worth showing (e.g. "couldn't download"), or "".
  virtual std::string problem() const { return {}; }
  virtual void request_access() = 0;  // e.g. the macOS prompt, or a sign-in
  virtual std::vector<CalendarInfo> calendars() = 0;
  // Events overlapping [from, to) in the chosen calendars (seconds since 1970).
  virtual std::vector<rma3::CalEvent> events(const std::vector<std::string>& calendar_ids, double from,
                                             double to) = 0;
  // True once after the calendar data changed.
  virtual bool take_changed() = 0;
  // Asks the provider to pull from its server now. Any change arrives later
  // through take_changed().
  virtual void refresh() {}
};

std::unique_ptr<CalendarProvider> make_calendar_provider(CalendarKind kind);
const char* calendar_kind_name(CalendarKind kind);

}  // namespace rma
