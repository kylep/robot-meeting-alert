// Reads the Mac's calendars (every account in System Settings > Internet
// Accounts: Google, Google Workspace, Exchange, iCloud...) through EventKit.
// Only start/end times and a few flags leave this file; titles, people and
// notes are never read out.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "calendar_provider.hpp"
#include "rma3/host.hpp"

namespace rma {

// The Mac Calendar app's data, through EventKit.
class CalendarSource : public CalendarProvider {
 public:
  CalendarSource();
  ~CalendarSource();
  CalendarSource(const CalendarSource&) = delete;
  CalendarSource& operator=(const CalendarSource&) = delete;

  const char* name() const override { return "Mac Calendar app"; }
  const char* help() const override {
    return "Uses every account added to the Mac's Calendar app (System Settings > Internet Accounts): "
           "Google, Google Workspace, Exchange, iCloud.";
  }
  void request_access() override;  // shows the macOS prompt the first time
  Access access() const override { return access_.load(); }
  std::vector<CalendarInfo> calendars() override;
  std::vector<rma3::CalEvent> events(const std::vector<std::string>& calendar_ids, double from,
                                     double to) override;
  bool take_changed() override { return changed_.exchange(false); }

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
  std::atomic<Access> access_{Access::Unknown};
  std::atomic<bool> changed_{false};
};

}  // namespace rma
