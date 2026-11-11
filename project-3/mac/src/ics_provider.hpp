// The "iCal link" calendar source: a published calendar's .ics address
// (Outlook on the web, Google Calendar's secret address, ...) downloaded
// every few minutes, or a local .ics file. Portable: the Mac and Windows
// apps share it; only fetch_url() differs (NSURLSession / WinHTTP).
//
// The link usually lets anyone read the calendar, so it lives only in
// pairing.hpp's private file and is never logged or shown in errors.
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "calendar_provider.hpp"

namespace rma {

// Downloads an https:// URL into *body (blocking, ~30 s timeout, size
// capped). On failure fills *error with a short reason that never contains
// the URL. Implemented per platform.
bool fetch_url(const std::string& url, std::string* body, std::string* error);

class IcsProvider : public CalendarProvider {
 public:
  static constexpr int kEveryS = 300;       // download every 5 minutes
  static constexpr int kRetryS = 60;        // sooner after a failure

  IcsProvider();  // reads the saved link
  ~IcsProvider() override;
  IcsProvider(const IcsProvider&) = delete;
  IcsProvider& operator=(const IcsProvider&) = delete;

  const char* name() const override;
  const char* help() const override;
  Access access() const override;
  std::string denied_help() const override;
  std::string problem() const override;
  void request_access() override;  // starts downloading
  std::vector<CalendarInfo> calendars() override;
  std::vector<rma3::CalEvent> events(const std::vector<std::string>& calendar_ids, double from,
                                     double to) override;
  bool take_changed() override { return changed_.exchange(false); }

 private:
  void run();
  bool load(std::string* text, std::string* error) const;

  std::string link_;  // set once in the constructor
  std::atomic<Access> access_{Access::Unknown};
  std::atomic<bool> changed_{false};
  std::atomic<bool> stop_{false};
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::string text_;      // last good download
  std::string error_;     // last failure, "" after a success
  std::string name_;      // X-WR-CALNAME of the last good download
  int unsupported_ = 0;
  double fetched_at_ = 0;  // seconds since 1970 of the last good download
  std::thread worker_;
};

}  // namespace rma
