// iCalendar (.ics) reader for the "published calendar link" source. Host
// only (Mac and Windows apps, tests); not linked into firmware.
//
// Reads VEVENTs and turns them into CalEvents (times only; titles, places
// and people are never kept). Handles:
//   - UTC times, TZID times with the file's own VTIMEZONE blocks (Outlook
//     and Google both include them), and "floating" times (this computer's
//     time zone). A TZID with no VTIMEZONE is treated as floating.
//   - All-day events (VALUE=DATE), STATUS:CANCELLED, TRANSP:TRANSPARENT and
//     Outlook's X-MICROSOFT-CDO-BUSYSTATUS:FREE (both mean "free").
//   - A declined invite: an ATTENDEE with PARTSTAT=DECLINED whose address is
//     the calendar owner's (given, or taken from X-WR-CALNAME when that is
//     an email address, as in Google's secret iCal link).
//   - Repeats: RRULE FREQ=DAILY/WEEKLY/MONTHLY/YEARLY with INTERVAL, COUNT,
//     UNTIL, BYDAY (incl. "2MO"/"-1FR" in monthly and yearly rules),
//     BYMONTHDAY, BYMONTH and WKST; plus EXDATE, RDATE and moved or cancelled
//     single occurrences (RECURRENCE-ID).
// Not handled: BYSETPOS, BYWEEKNO, BYYEARDAY, BYHOUR/BYMINUTE/BYSECOND,
// HOURLY and finer rules, RANGE=THISANDFUTURE. Such a series shows only its
// first occurrence and is counted in IcsResult::unsupported.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "host.hpp"

namespace rma3 {

struct IcsResult {
  bool ok = false;           // false: not an iCalendar file at all
  std::string name;          // X-WR-CALNAME, if any (shown in the picker only)
  std::vector<CalEvent> events;  // occurrences overlapping [from, to)
  int unsupported = 0;       // repeating events whose rule couldn't be expanded, or past the file's limits
};

// `from`/`to` are seconds since 1970. `self` is the owner's email address
// (any case); empty = guess from X-WR-CALNAME.
IcsResult parse_ics(std::string_view text, double from, double to, const std::string& self = "");

}  // namespace rma3
