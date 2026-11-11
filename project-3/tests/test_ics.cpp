#include <algorithm>
#include <chrono>
// iCalendar reader: time zones, repeats, exceptions and the skip rules.
#include <string>
#include <vector>

#include "check.hpp"
#include "rma3/ics.hpp"

namespace {

// Seconds since 1970 for a UTC date and time.
double utc(int y, int m, int d, int h = 0, int mi = 0) {
  y -= m <= 2;
  long long era = (y >= 0 ? y : y - 399) / 400;
  long long yoe = y - era * 400;
  long long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  long long days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return static_cast<double>(days * 86400 + h * 3600 + mi * 60);
}

const char* kNewYork =
    "BEGIN:VTIMEZONE\r\n"
    "TZID:Eastern Standard Time\r\n"
    "BEGIN:STANDARD\r\n"
    "DTSTART:16010101T020000\r\n"
    "TZOFFSETFROM:-0400\r\n"
    "TZOFFSETTO:-0500\r\n"
    "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=1SU;BYMONTH=11\r\n"
    "END:STANDARD\r\n"
    "BEGIN:DAYLIGHT\r\n"
    "DTSTART:16010101T020000\r\n"
    "TZOFFSETFROM:-0500\r\n"
    "TZOFFSETTO:-0400\r\n"
    "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=2SU;BYMONTH=3\r\n"
    "END:DAYLIGHT\r\n"
    "END:VTIMEZONE\r\n";

std::string cal(const std::string& body, const std::string& head = "") {
  return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" + head + std::string(kNewYork) + body + "END:VCALENDAR\r\n";
}

std::string event(const std::string& props) {
  return "BEGIN:VEVENT\r\nUID:u1\r\n" + props + "END:VEVENT\r\n";
}

std::vector<double> starts(const rma3::IcsResult& r) {
  std::vector<double> out;
  for (const auto& e : r.events) out.push_back(e.start);
  return out;
}

void test_utc_and_window() {
  std::string text = cal(event("DTSTART:20261110T150000Z\r\nDTEND:20261110T153000Z\r\n"));
  auto r = rma3::parse_ics(text, utc(2026, 11, 10), utc(2026, 11, 11));
  CHECK(r.ok);
  CHECK(r.events.size() == 1);
  if (r.events.size() == 1) {
    CHECK(r.events[0].start == utc(2026, 11, 10, 15));
    CHECK(r.events[0].end == utc(2026, 11, 10, 15, 30));
    CHECK_STR(r.events[0].id, "u1");
  }
  CHECK(rma3::parse_ics(text, utc(2026, 11, 11), utc(2026, 11, 12)).events.empty());
  CHECK(!rma3::parse_ics("hello", 0, 1e10).ok);
}

void test_tzid_and_dst() {
  std::string summer = event("DTSTART;TZID=Eastern Standard Time:20260701T090000\r\nDURATION:PT1H\r\n");
  auto r = rma3::parse_ics(cal(summer), utc(2026, 7, 1), utc(2026, 7, 2));
  CHECK(r.events.size() == 1 && r.events[0].start == utc(2026, 7, 1, 13) && r.events[0].end == utc(2026, 7, 1, 14));
  std::string winter = event("DTSTART;TZID=\"Eastern Standard Time\":20261201T090000\r\n"
                             "DTEND;TZID=Eastern Standard Time:20261201T093000\r\n");
  r = rma3::parse_ics(cal(winter), utc(2026, 12, 1), utc(2026, 12, 2));
  CHECK(r.events.size() == 1 && r.events[0].start == utc(2026, 12, 1, 14) && r.events[0].end == utc(2026, 12, 1, 14, 30));
}

void test_weekly_across_dst() {
  // Every Monday, Wednesday and Friday at 9:00 New York time. Clocks go back
  // on Sunday 1 November 2026, so 13:00 UTC becomes 14:00 UTC.
  std::string e = event("DTSTART;TZID=Eastern Standard Time:20261026T090000\r\n"
                        "DTEND;TZID=Eastern Standard Time:20261026T091500\r\n"
                        "RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR\r\n");
  auto r = rma3::parse_ics(cal(e), utc(2026, 10, 26), utc(2026, 11, 7));
  std::vector<double> want = {utc(2026, 10, 26, 13), utc(2026, 10, 28, 13), utc(2026, 10, 30, 13),
                              utc(2026, 11, 2, 14),  utc(2026, 11, 4, 14),  utc(2026, 11, 6, 14)};
  CHECK(starts(r) == want);
  if (!r.events.empty()) CHECK(r.events[0].end - r.events[0].start == 15 * 60);

  // COUNT includes DTSTART; UNTIL is inclusive.
  r = rma3::parse_ics(cal(event("DTSTART:20261026T090000Z\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE;COUNT=3\r\n")),
                      utc(2026, 10, 1), utc(2026, 12, 1));
  CHECK((starts(r) == std::vector<double>{utc(2026, 10, 26, 9), utc(2026, 10, 28, 9), utc(2026, 11, 2, 9)}));
  r = rma3::parse_ics(cal(event("DTSTART:20261026T090000Z\r\nRRULE:FREQ=WEEKLY;UNTIL=20261109T090000Z\r\n")),
                      utc(2026, 10, 1), utc(2026, 12, 1));
  CHECK((starts(r) == std::vector<double>{utc(2026, 10, 26, 9), utc(2026, 11, 2, 9), utc(2026, 11, 9, 9)}));

  // Every other week, Tuesdays.
  r = rma3::parse_ics(cal(event("DTSTART:20261103T100000Z\r\nRRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TU\r\n")),
                      utc(2026, 11, 1), utc(2026, 12, 1));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 3, 10), utc(2026, 11, 17, 10)}));
}

void test_daily_and_far_past_start() {
  auto r = rma3::parse_ics(cal(event("DTSTART:20261110T080000Z\r\nRRULE:FREQ=DAILY;INTERVAL=2\r\n")),
                           utc(2026, 11, 10), utc(2026, 11, 15));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 10, 8), utc(2026, 11, 12, 8), utc(2026, 11, 14, 8)}));
  // A stand-up that started years ago still lands on today.
  r = rma3::parse_ics(cal(event("DTSTART;TZID=Eastern Standard Time:20150105T093000\r\n"
                                "RRULE:FREQ=DAILY;BYDAY=MO,TU,WE,TH,FR\r\n")),
                      utc(2026, 11, 11), utc(2026, 11, 12));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 11, 14, 30)}));
}

void test_monthly_yearly() {
  // Last Friday of the month.
  auto r = rma3::parse_ics(cal(event("DTSTART:20260925T160000Z\r\nRRULE:FREQ=MONTHLY;BYDAY=-1FR\r\n")),
                           utc(2026, 10, 1), utc(2026, 12, 1));
  CHECK((starts(r) == std::vector<double>{utc(2026, 10, 30, 16), utc(2026, 11, 27, 16)}));
  // The 15th of every month.
  r = rma3::parse_ics(cal(event("DTSTART:20260115T120000Z\r\nRRULE:FREQ=MONTHLY\r\n")),
                      utc(2026, 11, 1), utc(2026, 12, 31));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 15, 12), utc(2026, 12, 15, 12)}));
  r = rma3::parse_ics(cal(event("DTSTART:20201111T120000Z\r\nRRULE:FREQ=YEARLY\r\n")), utc(2026, 1, 1),
                      utc(2027, 1, 1));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 11, 12)}));
  // Unsupported rule parts: only the first occurrence, and counted.
  r = rma3::parse_ics(cal(event("DTSTART:20261102T120000Z\r\nRRULE:FREQ=MONTHLY;BYDAY=MO,TU;BYSETPOS=1\r\n")),
                      utc(2026, 11, 1), utc(2027, 1, 1));
  CHECK(r.unsupported == 1);
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 2, 12)}));
}

void test_exceptions() {
  std::string text = cal(
      event("DTSTART;TZID=Eastern Standard Time:20261102T100000\r\nDURATION:PT30M\r\n"
            "RRULE:FREQ=DAILY;COUNT=5\r\n"
            "EXDATE;TZID=Eastern Standard Time:20261103T100000\r\n") +
      // The 4 November one moved to 15:00.
      event("RECURRENCE-ID;TZID=Eastern Standard Time:20261104T100000\r\n"
            "DTSTART;TZID=Eastern Standard Time:20261104T150000\r\nDURATION:PT30M\r\n") +
      // The 5 November one was cancelled.
      event("RECURRENCE-ID;TZID=Eastern Standard Time:20261105T100000\r\n"
            "DTSTART;TZID=Eastern Standard Time:20261105T100000\r\nDURATION:PT30M\r\nSTATUS:CANCELLED\r\n"));
  auto r = rma3::parse_ics(text, utc(2026, 11, 1), utc(2026, 11, 10));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 2, 15), utc(2026, 11, 4, 20), utc(2026, 11, 5, 15),
                                          utc(2026, 11, 6, 15)}));
  CHECK(r.events.size() == 4 && r.events[2].cancelled && !r.events[1].cancelled);
}

void test_flags() {
  std::string text = cal(
      event("DTSTART;VALUE=DATE:20261111\r\nDTEND;VALUE=DATE:20261112\r\n") +
      event("DTSTART:20261111T130000Z\r\nDTEND:20261111T140000Z\r\nSTATUS:CANCELLED\r\n") +
      event("DTSTART:20261111T140000Z\r\nDTEND:20261111T150000Z\r\nTRANSP:TRANSPARENT\r\n") +
      event("DTSTART:20261111T150000Z\r\nDTEND:20261111T160000Z\r\nX-MICROSOFT-CDO-BUSYSTATUS:FREE\r\n") +
      event("DTSTART:20261111T160000Z\r\nDTEND:20261111T170000Z\r\n"
            "ATTENDEE;CN=Someone;PARTSTAT=ACCEPTED:mailto:other@example.com\r\n"
            "ATTENDEE;CN=Me;PARTSTAT=DECLINED;RSVP=TRUE:\r\n mailto:Me@Example.com\r\n") +
      event("DTSTART:20261111T170000Z\r\nDTEND:20261111T180000Z\r\nTRANSP:OPAQUE\r\n"),
      "X-WR-CALNAME:me@example.com\r\n");
  auto r = rma3::parse_ics(text, utc(2026, 11, 10), utc(2026, 11, 13));
  CHECK_STR(r.name, "me@example.com");
  CHECK(r.events.size() == 6);
  if (r.events.size() == 6) {
    CHECK(r.events[0].all_day);
    CHECK(r.events[1].cancelled);
    CHECK(r.events[2].free);
    CHECK(r.events[3].free);
    CHECK(r.events[4].declined);  // folded line, mixed case address
    const auto& e = r.events[5];
    CHECK(!e.all_day && !e.cancelled && !e.free && !e.declined);
    // Only the plain meeting would reach the robot.
    auto picked = rma3::pick_meetings(utc(2026, 11, 11, 12), r.events);
    CHECK(picked.size() == 1 && picked[0].start == 5 * 3600);
  }
  // Without a known owner, a declined attendee can't be told apart.
  std::string anon = cal(event("DTSTART:20261111T160000Z\r\nDTEND:20261111T170000Z\r\n"
                               "ATTENDEE;PARTSTAT=DECLINED:mailto:me@example.com\r\n"));
  r = rma3::parse_ics(anon, utc(2026, 11, 10), utc(2026, 11, 13));
  CHECK(r.events.size() == 1 && !r.events[0].declined);
  r = rma3::parse_ics(anon, utc(2026, 11, 10), utc(2026, 11, 13), "ME@example.com");
  CHECK(r.events.size() == 1 && r.events[0].declined);
}

void test_line_endings_and_alarms() {
  // Plain LF line endings, an alarm inside the event, and an unknown TZID
  // given as UTC.
  std::string text =
      "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:x\nDTSTART;TZID=UTC:20261111T100000\nDTEND;TZID=UTC:20261111T110000\n"
      "BEGIN:VALARM\nTRIGGER:-PT15M\nACTION:DISPLAY\nEND:VALARM\nEND:VEVENT\nEND:VCALENDAR\n";
  auto r = rma3::parse_ics(text, utc(2026, 11, 11), utc(2026, 11, 12));
  CHECK((starts(r) == std::vector<double>{utc(2026, 11, 11, 10)}));
  if (!r.events.empty()) CHECK(r.events[0].end == utc(2026, 11, 11, 11));
}

void test_hostile_feed_is_bounded() {
  // 6000 events, each a COUNT rule from 1900 (no jump-ahead), plus one normal
  // meeting first. The parse must stay fast and keep the normal meeting.
  std::string text =
      "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:ok\nDTSTART:20261111T100000Z\nDTEND:20261111T110000Z\nEND:VEVENT\n";
  for (int i = 0; i < 6000; ++i) {
    text += "BEGIN:VEVENT\nUID:h" + std::to_string(i) +
            "\nDTSTART:19000101T090000Z\nDTEND:19000101T100000Z\nRRULE:FREQ=DAILY;COUNT=999999999\nEND:VEVENT\n";
  }
  text += "END:VCALENDAR\n";
  auto t0 = std::chrono::steady_clock::now();
  auto r = rma3::parse_ics(text, utc(2026, 11, 11), utc(2026, 11, 12));
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK(secs < 5.0);
  CHECK(r.unsupported > 0);
  CHECK(std::any_of(r.events.begin(), r.events.end(), [](const auto& e) { return e.start == utc(2026, 11, 11, 10); }));
}

}  // namespace

int main() {
  test_utc_and_window();
  test_tzid_and_dst();
  test_weekly_across_dst();
  test_daily_and_far_past_start();
  test_monthly_yearly();
  test_exceptions();
  test_flags();
  test_line_endings_and_alarms();
  test_hostile_feed_is_bounded();
  if (g_failures == 0) std::printf("test_ics: all passed\n");
  return g_failures == 0 ? 0 : 1;
}
