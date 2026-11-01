// Reads the Mac's calendars (every account in System Settings > Internet
// Accounts: Google, Google Workspace, Exchange, iCloud...) through EventKit.
// Only start/end times and a few flags leave this file; titles, people and
// notes are never read out.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "rma3/host.hpp"

namespace rma {

struct CalendarInfo {
  std::string id;       // EventKit calendarIdentifier
  std::string title;    // shown in the app's picker only
  std::string account;  // e.g. "Google" / the account's name
  float rgb[3] = {0.5f, 0.5f, 0.5f};
};

class CalendarSource {
 public:
  enum class Access { Unknown, Requesting, Granted, Denied };

  CalendarSource();
  ~CalendarSource();
  CalendarSource(const CalendarSource&) = delete;
  CalendarSource& operator=(const CalendarSource&) = delete;

  void request_access();  // shows the macOS prompt the first time
  Access access() const { return access_.load(); }
  std::vector<CalendarInfo> calendars();
  // Events overlapping [from, to) in the chosen calendars (seconds since 1970).
  std::vector<rma3::CalEvent> events(const std::vector<std::string>& calendar_ids, double from, double to);
  // True once after the calendar database changed.
  bool take_changed() { return changed_.exchange(false); }

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
  std::atomic<Access> access_{Access::Unknown};
  std::atomic<bool> changed_{false};
};

}  // namespace rma
