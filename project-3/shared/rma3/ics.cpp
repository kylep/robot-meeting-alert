#include "ics.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <utility>

namespace rma3 {
namespace {

using i64 = long long;
constexpr i64 kDay = 86400;

// ---------------------------------------------------------------- dates
// "Wall" times are clock readings in some time zone, counted in seconds
// since 1970-01-01 00:00 as if that zone were UTC.

i64 days_from_civil(i64 y, int m, int d) {
  y -= m <= 2;
  const i64 era = (y >= 0 ? y : y - 399) / 400;
  const i64 yoe = y - era * 400;
  const i64 doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const i64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civil_from_days(i64 z, int* y, int* m, int* d) {
  z += 719468;
  const i64 era = (z >= 0 ? z : z - 146096) / 146097;
  const i64 doe = z - era * 146097;
  const i64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const i64 mp = (5 * doy + 2) / 153;
  *d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  *m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  *y = static_cast<int>(yoe + era * 400 + (*m <= 2));
}

i64 floor_div(i64 a, i64 b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

int weekday(i64 days) { return static_cast<int>(((days % 7) + 11) % 7); }  // 0 = Sunday

int days_in_month(int y, int m) {
  return static_cast<int>(days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - days_from_civil(y, m, 1));
}

// Day of month of the n-th (1..5, or -1..-5 from the end) weekday `wd`; 0 if none.
int nth_weekday(int y, int m, int n, int wd) {
  int dim = days_in_month(y, m);
  if (n > 0) {
    int first = weekday(days_from_civil(y, m, 1));
    int d = 1 + (wd - first + 7) % 7 + (n - 1) * 7;
    return d <= dim ? d : 0;
  }
  int last = weekday(days_from_civil(y, m, dim));
  int d = dim - (last - wd + 7) % 7 + (n + 1) * 7;
  return d >= 1 ? d : 0;
}

int weekday_code(std::string_view s) {
  static const char* kCodes[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  for (int i = 0; i < 7; ++i) {
    if (s == kCodes[i]) return i;
  }
  return -1;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t i = 0;
  for (;;) {
    size_t j = s.find(sep, i);
    out.push_back(s.substr(i, j == std::string::npos ? std::string::npos : j - i));
    if (j == std::string::npos) return out;
    i = j + 1;
  }
}

bool digits(std::string_view s, size_t pos, size_t n, int* out) {
  if (pos + n > s.size()) return false;
  int v = 0;
  for (size_t i = pos; i < pos + n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return true;
}

// ---------------------------------------------------------------- lines

struct Prop {
  std::string name;
  std::map<std::string, std::string> params;
  std::string value;
  std::string param(const char* key) const {
    auto it = params.find(key);
    return it == params.end() ? std::string() : it->second;
  }
};

bool parse_prop(const std::string& line, Prop* p) {
  size_t i = 0;
  while (i < line.size() && line[i] != ';' && line[i] != ':') ++i;
  p->name = upper(line.substr(0, i));
  p->params.clear();
  while (i < line.size() && line[i] == ';') {
    size_t eq = line.find('=', ++i);
    if (eq == std::string::npos) return false;
    std::string key = upper(line.substr(i, eq - i));
    std::string val;
    bool quoted = false;
    for (i = eq + 1; i < line.size(); ++i) {
      char c = line[i];
      if (c == '"') {
        quoted = !quoted;
        continue;
      }
      if (!quoted && (c == ';' || c == ':')) break;
      val += c;
    }
    p->params[key] = val;
  }
  if (i >= line.size() || line[i] != ':') return false;
  p->value = line.substr(i + 1);
  return true;
}

// Splits into logical lines: CRLF or LF, folded lines joined.
std::vector<std::string> unfold(std::string_view text) {
  std::vector<std::string> lines;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string_view::npos) j = text.size();
    std::string_view l = text.substr(i, j - i);
    if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
    if (!l.empty() && (l[0] == ' ' || l[0] == '\t') && !lines.empty()) {
      lines.back().append(l.substr(1));
    } else if (!l.empty()) {
      lines.emplace_back(l);
    }
    i = j + 1;
  }
  return lines;
}

// ---------------------------------------------------------------- times

struct Dt {
  bool valid = false;
  i64 wall = 0;
  bool date = false;  // VALUE=DATE: a whole day
  bool utc = false;   // trailing Z
  std::string tz;     // TZID, if any
};

Dt parse_dt(std::string_view v, const std::string& tz, bool date_param) {
  Dt dt;
  int y, mo, d, h = 0, mi = 0, s = 0;
  if (!digits(v, 0, 4, &y) || !digits(v, 4, 2, &mo) || !digits(v, 6, 2, &d)) return dt;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return dt;
  if (v.size() >= 15 && v[8] == 'T' && !date_param) {
    if (!digits(v, 9, 2, &h) || !digits(v, 11, 2, &mi) || !digits(v, 13, 2, &s)) return dt;
    dt.utc = v.size() >= 16 && (v[15] == 'Z' || v[15] == 'z');
  } else {
    dt.date = true;
  }
  dt.wall = days_from_civil(y, mo, d) * kDay + h * 3600 + mi * 60 + s;
  dt.tz = dt.utc || dt.date ? std::string() : tz;
  dt.valid = true;
  return dt;
}

Dt prop_dt(const Prop& p) { return parse_dt(p.value, p.param("TZID"), upper(p.param("VALUE")) == "DATE"); }

std::vector<Dt> prop_dts(const Prop& p) {
  std::vector<Dt> out;
  bool date = upper(p.param("VALUE")) == "DATE";
  for (const std::string& v : split(p.value, ',')) {
    Dt dt = parse_dt(v, p.param("TZID"), date);
    if (dt.valid) out.push_back(dt);
  }
  return out;
}

// "+0100", "-0500", "+053000" -> seconds east of UTC.
bool parse_offset(const std::string& v, int* out) {
  int h, m, s = 0;
  if (v.size() < 5 || (v[0] != '+' && v[0] != '-') || !digits(v, 1, 2, &h) || !digits(v, 3, 2, &m)) return false;
  if (v.size() >= 7) digits(v, 5, 2, &s);
  *out = (v[0] == '-' ? -1 : 1) * (h * 3600 + m * 60 + s);
  return true;
}

// "P1D", "PT1H30M", "P2W" -> seconds; -1 if unreadable.
i64 parse_duration(const std::string& v) {
  size_t i = 0;
  bool neg = false;
  if (i < v.size() && (v[i] == '+' || v[i] == '-')) neg = v[i++] == '-';
  if (i >= v.size() || v[i] != 'P') return -1;
  ++i;
  i64 total = 0, n = 0;
  bool have = false;
  for (; i < v.size(); ++i) {
    char c = v[i];
    if (c == 'T') continue;
    if (c >= '0' && c <= '9') {
      n = n * 10 + (c - '0');
      have = true;
      continue;
    }
    if (!have) return -1;
    switch (c) {
      case 'W': total += n * 7 * kDay; break;
      case 'D': total += n * kDay; break;
      case 'H': total += n * 3600; break;
      case 'M': total += n * 60; break;
      case 'S': total += n; break;
      default: return -1;
    }
    n = 0;
    have = false;
  }
  return neg ? -1 : total;
}

// One STANDARD or DAYLIGHT block of a VTIMEZONE.
struct Observance {
  Dt start;
  int off_from = 0, off_to = 0;
  bool yearly = false;
  int month = 0, nth = 0, wd = -1, monthday = 0;
  int until_year = INT_MAX;
  std::vector<i64> rdates;  // wall times
};

struct Zone {
  std::vector<Observance> obs;
};

// When an observance begins in year y (wall time), or false if it doesn't.
bool onset(const Observance& o, int y, i64* out) {
  int y0, m0, d0;
  civil_from_days(floor_div(o.start.wall, kDay), &y0, &m0, &d0);
  if (y < y0 || y > o.until_year) return false;
  int m = o.month ? o.month : m0;
  int d = o.wd >= 0 ? nth_weekday(y, m, o.nth ? o.nth : 1, o.wd) : (o.monthday ? o.monthday : d0);
  if (d <= 0 || d > days_in_month(y, m)) return false;
  *out = days_from_civil(y, m, d) * kDay + (o.start.wall - floor_div(o.start.wall, kDay) * kDay);
  return true;
}

// UTC offset in force at wall time t.
int zone_offset(const Zone& z, i64 t) {
  int y, m, d;
  civil_from_days(floor_div(t, kDay), &y, &m, &d);
  i64 best = LLONG_MIN, earliest = LLONG_MAX;
  int off = 0, earliest_from = 0;
  for (const Observance& o : z.obs) {
    auto consider = [&](i64 s) {
      if (s <= t && s > best) {
        best = s;
        off = o.off_to;
      }
    };
    for (i64 s : o.rdates) consider(s);
    if (o.yearly) {
      for (int yy : {y - 1, y}) {
        i64 s;
        if (onset(o, yy, &s)) consider(s);
      }
    } else {
      consider(o.start.wall);
    }
    if (o.start.wall < earliest) {
      earliest = o.start.wall;
      earliest_from = o.off_from;
    }
  }
  return best == LLONG_MIN ? earliest_from : off;
}

// A wall time in this computer's own time zone, to UTC.
i64 floating_to_utc(i64 wall) {
  int y, m, d;
  i64 days = floor_div(wall, kDay);
  civil_from_days(days, &y, &m, &d);
  i64 sec = wall - days * kDay;
  std::tm t{};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = static_cast<int>(sec / 3600);
  t.tm_min = static_cast<int>(sec / 60 % 60);
  t.tm_sec = static_cast<int>(sec % 60);
  t.tm_isdst = -1;
  std::time_t r = std::mktime(&t);
  return r == static_cast<std::time_t>(-1) ? wall : static_cast<i64>(r);
}

struct Zones {
  std::map<std::string, Zone> by_id;

  i64 to_utc(const Dt& dt) const {
    if (dt.utc) return dt.wall;
    if (!dt.tz.empty()) {
      std::string u = upper(dt.tz);
      if (u == "UTC" || u == "GMT" || u == "ETC/UTC" || u == "ETC/GMT" || u == "Z") return dt.wall;
      auto it = by_id.find(dt.tz);
      if (it != by_id.end() && !it->second.obs.empty()) return dt.wall - zone_offset(it->second, dt.wall);
    }
    return floating_to_utc(dt.wall);
  }
};

// ---------------------------------------------------------------- events

struct Rule {
  std::string freq;
  int interval = 1;
  int count = 0;
  Dt until;
  std::vector<std::pair<int, int>> byday;  // (n, weekday); n = 0 for "every"
  std::vector<int> bymonthday, bymonth;
  int wkst = 1;  // Monday
  bool supported = true;
};

std::vector<int> int_list(const std::string& v) {
  std::vector<int> out;
  for (const std::string& s : split(v, ',')) {
    if (!s.empty()) out.push_back(std::atoi(s.c_str()));
  }
  return out;
}

Rule parse_rule(const std::string& v, const std::string& tz) {
  Rule r;
  for (const std::string& part : split(v, ';')) {
    size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    std::string key = upper(part.substr(0, eq)), val = upper(part.substr(eq + 1));
    if (key == "FREQ") {
      r.freq = val;
    } else if (key == "INTERVAL") {
      r.interval = std::max(1, std::atoi(val.c_str()));
    } else if (key == "COUNT") {
      r.count = std::max(1, std::atoi(val.c_str()));
    } else if (key == "UNTIL") {
      r.until = parse_dt(val, tz, false);
    } else if (key == "BYDAY") {
      for (const std::string& s : split(val, ',')) {
        if (s.size() < 2) continue;
        int wd = weekday_code(s.substr(s.size() - 2));
        if (wd < 0) {
          r.supported = false;
          continue;
        }
        int n = s.size() > 2 ? std::atoi(s.substr(0, s.size() - 2).c_str()) : 0;
        r.byday.emplace_back(n, wd);
      }
    } else if (key == "BYMONTHDAY") {
      r.bymonthday = int_list(val);
    } else if (key == "BYMONTH") {
      r.bymonth = int_list(val);
    } else if (key == "WKST") {
      int wd = weekday_code(val);
      if (wd >= 0) r.wkst = wd;
    } else {
      r.supported = false;  // BYSETPOS, BYWEEKNO, BYYEARDAY, BYHOUR, ...
    }
  }
  if (r.freq != "DAILY" && r.freq != "WEEKLY" && r.freq != "MONTHLY" && r.freq != "YEARLY") r.supported = false;
  for (int m : r.bymonth) {
    if (m < 1 || m > 12) r.supported = false;
  }
  for (int d : r.bymonthday) {
    if (d == 0 || d < -31 || d > 31) r.supported = false;
  }
  for (auto [n, wd] : r.byday) {
    if (n != 0 && (r.freq == "DAILY" || r.freq == "WEEKLY" || n < -5 || n > 5)) r.supported = false;
  }
  // "The 20th Monday of the year" style rules.
  if (r.freq == "YEARLY" && !r.byday.empty() && r.bymonth.empty()) r.supported = false;
  return r;
}

// Limits for untrusted feeds: repeat periods expanded per file, and events kept.
constexpr i64 kExpandBudget = 300000;
constexpr size_t kMaxEvents = 5000;
constexpr size_t kMaxOverrides = 20000;  // moved/cancelled single meetings
constexpr size_t kMaxZoneParts = 16;     // observances per time zone
constexpr size_t kMaxZoneDates = 400;    // RDATEs per time zone, all observances together
constexpr size_t kMaxEventDates = 400;   // EXDATEs, and RDATEs, per event
constexpr size_t kMaxExdates = 20000;    // EXDATEs in the whole file

struct Raw {
  std::string uid;
  Dt start, end, recur_id;
  i64 duration = -1;
  std::string rrule;
  std::vector<Dt> exdates, rdates;
  bool cancelled = false, free = false, all_day = false;
  std::vector<std::pair<std::string, std::string>> attendees;  // (address, PARTSTAT)
};

void add_event_prop(Raw* e, const Prop& p) {
  const std::string& n = p.name;
  if (n == "UID") e->uid = p.value;
  else if (n == "DTSTART") e->start = prop_dt(p);
  else if (n == "DTEND") e->end = prop_dt(p);
  else if (n == "DURATION") e->duration = parse_duration(upper(p.value));
  else if (n == "RECURRENCE-ID") e->recur_id = prop_dt(p);
  else if (n == "RRULE") e->rrule = p.value;
  else if (n == "EXDATE") {
    for (const Dt& d : prop_dts(p)) {
      if (e->exdates.size() < kMaxEventDates) e->exdates.push_back(d);
    }
  }
  else if (n == "RDATE") {
    for (const Dt& d : prop_dts(p)) {
      if (e->rdates.size() < kMaxEventDates) e->rdates.push_back(d);
    }
  }
  else if (n == "STATUS") e->cancelled = upper(p.value) == "CANCELLED";
  else if (n == "TRANSP") e->free = e->free || upper(p.value) == "TRANSPARENT";
  else if (n == "X-MICROSOFT-CDO-BUSYSTATUS") e->free = e->free || upper(p.value) == "FREE";
  else if (n == "X-MICROSOFT-CDO-ALLDAYEVENT") e->all_day = e->all_day || upper(p.value) == "TRUE";
  else if (n == "ATTENDEE") {
    std::string addr = lower(p.value);
    if (addr.rfind("mailto:", 0) == 0) addr = addr.substr(7);
    e->attendees.emplace_back(addr, upper(p.param("PARTSTAT")));
  }
}

void add_observance_prop(Observance* o, const Prop& p) {
  if (p.name == "DTSTART") {
    o->start = parse_dt(p.value, "", false);
  } else if (p.name == "TZOFFSETFROM") {
    parse_offset(p.value, &o->off_from);
  } else if (p.name == "TZOFFSETTO") {
    parse_offset(p.value, &o->off_to);
  } else if (p.name == "RDATE") {
    for (const Dt& d : prop_dts(p)) {
      if (o->rdates.size() < kMaxZoneDates) o->rdates.push_back(d.wall);
    }
  } else if (p.name == "RRULE") {
    Rule r = parse_rule(p.value, "");
    if (r.freq != "YEARLY") return;
    o->yearly = true;
    if (!r.bymonth.empty()) o->month = r.bymonth[0];
    if (!r.byday.empty()) {
      o->nth = r.byday[0].first;
      o->wd = r.byday[0].second;
    }
    if (!r.bymonthday.empty()) o->monthday = r.bymonthday[0];
    if (r.until.valid) {
      int y, m, d;
      civil_from_days(floor_div(r.until.wall, kDay), &y, &m, &d);
      o->until_year = y;
    }
  }
}

// Days of month `m` picked by the rule's BYMONTHDAY / BYDAY (or `day0`).
void month_days(const Rule& r, int y, int m, int day0, std::vector<i64>* out) {
  int dim = days_in_month(y, m);
  std::set<int> days;
  if (r.byday.empty() && r.bymonthday.empty()) {
    if (day0 <= dim) days.insert(day0);
  }
  std::set<int> by_md;
  for (int md : r.bymonthday) {
    int d = md > 0 ? md : dim + md + 1;
    if (d >= 1 && d <= dim) by_md.insert(d);
  }
  std::set<int> by_wd;
  for (auto [n, wd] : r.byday) {
    if (n != 0) {
      int d = nth_weekday(y, m, n, wd);
      if (d) by_wd.insert(d);
    } else {
      for (int d = 1; d <= dim; ++d) {
        if (weekday(days_from_civil(y, m, d)) == wd) by_wd.insert(d);
      }
    }
  }
  if (!r.bymonthday.empty() && !r.byday.empty()) {
    for (int d : by_md) {
      if (by_wd.count(d)) days.insert(d);
    }
  } else {
    days.insert(by_md.begin(), by_md.end());
    days.insert(by_wd.begin(), by_wd.end());
  }
  for (int d : days) out->push_back(days_from_civil(y, m, d));
}

bool in(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

class Expander {
 public:
  Expander(const Zones& zones, double from, double to) : zones_(zones), from_(from), to_(to) {}

  // Calls emit(wall) for every occurrence start of e (wall time in e's zone)
  // that could overlap [from, to). Returns false if the rule isn't supported
  // (then only DTSTART is emitted).
  template <typename Emit>
  bool expand(const Raw& e, Emit emit) {
    emit(e.start.wall);
    budget_ -= static_cast<i64>(e.rdates.size());
    if (budget_ < 0) return false;
    for (const Dt& rd : e.rdates) {
      Dt x = rd;
      if (x.tz.empty() && !x.utc) x.tz = e.start.tz;
      // An RDATE in another zone: shift into the event's zone via UTC.
      i64 utc = zones_.to_utc(x);
      emit(e.start.wall + (utc - zones_.to_utc(e.start)));
    }
    if (e.rrule.empty()) return true;
    Rule r = parse_rule(e.rrule, e.start.tz);
    if (!r.supported) return false;

    const i64 d0 = floor_div(e.start.wall, kDay);
    const i64 tod = e.start.wall - d0 * kDay;
    int y0, m0, md0;
    civil_from_days(d0, &y0, &m0, &md0);
    const i64 last_day = floor_div(static_cast<i64>(to_), kDay) + 2;  // wall vs UTC slack

    i64 k = 0;
    if (r.count == 0) {  // skip whole periods that end before the window
      i64 first_day = floor_div(static_cast<i64>(from_), kDay) - 40;
      if (first_day > d0) {
        int y1, m1, dd1;
        civil_from_days(first_day, &y1, &m1, &dd1);
        i64 span = 0;
        if (r.freq == "DAILY") span = (first_day - d0) / r.interval;
        else if (r.freq == "WEEKLY") span = (first_day - d0) / (7 * r.interval);
        else if (r.freq == "MONTHLY") span = ((y1 - y0) * 12 + (m1 - m0)) / r.interval;
        else span = (y1 - y0) / r.interval;
        k = std::max<i64>(0, span - 1);
      }
    }
    int seen = 1;  // DTSTART is always the first occurrence
    // One budget shared by every event in the file, so a hostile or huge feed
    // can't stall the app: once it runs out, the rest count as unsupported.
    for (;; ++k) {
      if (--budget_ < 0) return false;
      std::vector<i64> days;
      i64 period_start = 0;
      if (r.freq == "DAILY") {
        period_start = d0 + k * r.interval;
        int y, m, d;
        civil_from_days(period_start, &y, &m, &d);
        bool ok = (r.byday.empty() || std::any_of(r.byday.begin(), r.byday.end(),
                                                  [&](auto b) { return b.second == weekday(period_start); })) &&
                  (r.bymonth.empty() || in(r.bymonth, m)) &&
                  (r.bymonthday.empty() || in(r.bymonthday, d) || in(r.bymonthday, d - days_in_month(y, m) - 1));
        if (ok) days.push_back(period_start);
      } else if (r.freq == "WEEKLY") {
        period_start = d0 - (weekday(d0) - r.wkst + 7) % 7 + k * 7 * r.interval;
        std::vector<int> wds;
        for (auto b : r.byday) wds.push_back(b.second);
        if (wds.empty()) wds.push_back(weekday(d0));
        for (int wd : wds) {
          i64 day = period_start + (wd - r.wkst + 7) % 7;
          int y, m, d;
          civil_from_days(day, &y, &m, &d);
          if (r.bymonth.empty() || in(r.bymonth, m)) days.push_back(day);
        }
      } else if (r.freq == "MONTHLY") {
        i64 mi = (static_cast<i64>(y0) * 12 + (m0 - 1)) + k * r.interval;
        int y = static_cast<int>(floor_div(mi, 12)), m = static_cast<int>(mi - floor_div(mi, 12) * 12) + 1;
        period_start = days_from_civil(y, m, 1);
        if (r.bymonth.empty() || in(r.bymonth, m)) month_days(r, y, m, md0, &days);
      } else {  // YEARLY
        int y = static_cast<int>(y0 + k * r.interval);
        period_start = days_from_civil(y, 1, 1);
        std::vector<int> months = r.bymonth.empty() ? std::vector<int>{m0} : r.bymonth;
        std::sort(months.begin(), months.end());
        for (int m : months) month_days(r, y, m, md0, &days);
      }
      // Charge for the work done, so long BYDAY/BYMONTHDAY lists can't stretch the budget.
      budget_ -= static_cast<i64>(days.size() + r.byday.size() + r.bymonthday.size() + r.bymonth.size());
      if (period_start > last_day) return true;
      std::sort(days.begin(), days.end());
      for (i64 day : days) {
        if (day <= d0) continue;
        i64 wall = day * kDay + tod;
        if (r.until.valid && past_until(r.until, e.start, wall)) return true;
        if (r.count && ++seen > r.count) return true;
        emit(wall);
      }
    }
    return true;
  }

 private:
  bool past_until(const Dt& until, const Dt& start, i64 wall) const {
    if (until.date) return floor_div(wall, kDay) > floor_div(until.wall, kDay);
    Dt occ = start;
    occ.wall = wall;
    return zones_.to_utc(occ) > zones_.to_utc(until);
  }

  const Zones& zones_;
  double from_, to_;
  i64 budget_ = kExpandBudget;
};

}  // namespace

IcsResult parse_ics(std::string_view text, double from, double to, const std::string& self_in) {
  IcsResult res;
  Zones zones;
  std::vector<Raw> raws;
  size_t masters = 0, overrides = 0;
  std::vector<std::string> stack;
  std::string zone_id;
  Zone zone;
  Observance obs;
  Raw ev;

  for (const std::string& line : unfold(text)) {
    Prop p;
    if (!parse_prop(line, &p)) continue;
    if (p.name == "BEGIN") {
      std::string what = upper(p.value);
      if (what == "VCALENDAR") res.ok = true;
      if (what == "VEVENT") ev = Raw{};
      if (what == "VTIMEZONE") {
        zone = Zone{};
        zone_id.clear();
      }
      if ((what == "STANDARD" || what == "DAYLIGHT") && !stack.empty() && stack.back() == "VTIMEZONE") obs = Observance{};
      stack.push_back(what);
      continue;
    }
    if (p.name == "END") {
      std::string what = upper(p.value);
      if (stack.empty() || stack.back() != what) continue;  // unbalanced: ignore
      stack.pop_back();
      if (what == "VEVENT" && ev.start.valid) {
        // Overrides get their own (larger) cap, so a flood of them after the
        // masters can't push out the cancellations that matter.
        if (ev.recur_id.valid ? overrides < kMaxOverrides : masters < kMaxEvents) {
          raws.push_back(ev);
          ++(ev.recur_id.valid ? overrides : masters);
        } else {
          ++res.unsupported;
        }
      }
      if (what == "VTIMEZONE" && !zone_id.empty()) zones.by_id[zone_id] = zone;
      if ((what == "STANDARD" || what == "DAYLIGHT") && !stack.empty() && stack.back() == "VTIMEZONE" &&
          obs.start.valid) {
        if (zone.obs.size() < kMaxZoneParts) {
          size_t used = 0;
          for (const Observance& o : zone.obs) used += o.rdates.size();
          obs.rdates.resize(std::min(obs.rdates.size(), kMaxZoneDates - std::min(used, kMaxZoneDates)));
          zone.obs.push_back(obs);
        }
      }
      continue;
    }
    if (stack.empty()) continue;
    const std::string& top = stack.back();
    if (top == "VCALENDAR" && p.name == "X-WR-CALNAME") res.name = p.value;
    else if (top == "VEVENT") add_event_prop(&ev, p);
    else if (top == "VTIMEZONE" && p.name == "TZID") zone_id = p.value;
    else if (top == "STANDARD" || top == "DAYLIGHT") add_observance_prop(&obs, p);
  }
  if (!res.ok) return res;

  std::string self = lower(self_in);
  if (self.empty() && res.name.find('@') != std::string::npos) self = lower(res.name);

  // Single occurrences that were moved, cancelled or re-timed (RECURRENCE-ID).
  std::map<std::string, std::set<i64>> moved;
  for (const Raw& e : raws) {
    if (e.recur_id.valid) moved[e.uid].insert(zones.to_utc(e.recur_id));
  }

  Expander expander(zones, from, to);
  size_t exdates_seen = 0;
  for (const Raw& e : raws) {
    i64 start_utc = zones.to_utc(e.start);
    i64 dur = 0;
    if (e.end.valid) dur = zones.to_utc(e.end) - start_utc;
    else if (e.duration >= 0) dur = e.duration;
    else if (e.start.date) dur = kDay;
    if (dur < 0) dur = 0;
    CalEvent base;
    base.id = e.uid;
    base.all_day = e.start.date || e.all_day;
    base.free = e.free;
    base.cancelled = e.cancelled;
    for (const auto& [addr, partstat] : e.attendees) {
      if (!self.empty() && addr == self && partstat == "DECLINED") base.declined = true;
    }
    if (e.recur_id.valid) {  // stands on its own
      if (start_utc + dur > from && start_utc < to) {
        CalEvent c = base;
        c.start = static_cast<double>(start_utc);
        c.end = static_cast<double>(start_utc + dur);
        res.events.push_back(c);
      }
      continue;
    }
    std::set<i64> ex_utc, ex_days;
    for (const Dt& x : e.exdates) {
      if (++exdates_seen > kMaxExdates) {  // past the file's limit: report, stop
        ++res.unsupported;
        break;
      }
      if (x.date) {
        ex_days.insert(floor_div(x.wall, kDay));
      } else {
        Dt y = x;
        if (y.tz.empty() && !y.utc) y.tz = e.start.tz;
        ex_utc.insert(zones.to_utc(y));
      }
    }
    const std::set<i64>* skip = moved.count(e.uid) ? &moved[e.uid] : nullptr;
    std::set<i64> emitted;
    bool ok = expander.expand(e, [&](i64 wall) {
      Dt occ = e.start;
      occ.wall = wall;
      i64 s = zones.to_utc(occ);
      if (!emitted.insert(s).second) return;
      if (ex_utc.count(s) || ex_days.count(floor_div(wall, kDay))) return;
      if (skip && skip->count(s)) return;
      if (s + dur <= from || s >= to) return;
      CalEvent c = base;
      c.start = static_cast<double>(s);
      c.end = static_cast<double>(s + dur);
      res.events.push_back(c);
    });
    if (!ok) ++res.unsupported;
  }
  std::sort(res.events.begin(), res.events.end(),
            [](const CalEvent& a, const CalEvent& b) { return a.start < b.start; });
  return res;
}

}  // namespace rma3
