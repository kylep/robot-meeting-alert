// Placeholders for the work-laptop calendar options (docs/work-calendar.md).
// They show up in Settings so the choice is visible, and say what building
// them would take; they never return events.
#include <memory>

#include "calendar.hpp"
#include "calendar_provider.hpp"

namespace rma {
namespace {

class NotBuilt : public CalendarProvider {
 public:
  NotBuilt(const char* name, const char* help) : name_(name), help_(help) {}
  const char* name() const override { return name_; }
  const char* help() const override { return help_; }
  Access access() const override { return Access::NotBuilt; }
  void request_access() override {}
  std::vector<CalendarInfo> calendars() override { return {}; }
  std::vector<rma3::CalEvent> events(const std::vector<std::string>&, double, double) override { return {}; }
  bool take_changed() override { return false; }

 private:
  const char* name_;
  const char* help_;
};

}  // namespace

const char* calendar_kind_name(CalendarKind kind) {
  switch (kind) {
    case CalendarKind::kGoogleApi: return "Google sign-in (not built yet)";
    case CalendarKind::kIcsLink: return "Secret iCal link (not built yet)";
    default: return "Mac Calendar app";
  }
}

std::unique_ptr<CalendarProvider> make_calendar_provider(CalendarKind kind) {
  switch (kind) {
    case CalendarKind::kGoogleApi:
      // TODO(work laptop): OAuth 2.0 desktop flow (system browser + loopback
      // redirect), read-only events scope, refresh token in the Keychain,
      // Calendar API events.list with singleEvents=true (expands repeats).
      // Needs a Google Cloud OAuth client; a Workspace admin may have to allow it.
      return std::make_unique<NotBuilt>(
          calendar_kind_name(kind),
          "Signs in with Google and reads your work calendar directly. Use this if the Mac's Calendar app "
          "can't add your work account. Needs a Google Cloud OAuth client, and your Workspace admin may need "
          "to allow the app.");
    case CalendarKind::kIcsLink:
      // TODO(work laptop): HTTPS GET of the secret address, parse VEVENTs
      // (DTSTART/DTEND/TZID, RRULE/EXDATE/RECURRENCE-ID, STATUS, TRANSP),
      // keep the URL in the Keychain (it is a password in disguise).
      return std::make_unique<NotBuilt>(
          calendar_kind_name(kind),
          "Reads the calendar's \"secret address in iCal format\" from Google Calendar settings. Many "
          "Workspace admins turn that address off. The link works like a password, so it would be kept in "
          "the Keychain, never in the settings file.");
    default:
      return std::make_unique<CalendarSource>();
  }
}

}  // namespace rma
