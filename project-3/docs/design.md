# Project 3 design: wireless calendar alerts (the finished product)

Status: BUILT in software (2026-11-01): host tests, firmware and app build; hardware acceptance (§9) pending. Reviewed (opus, fresh context; fixes merged, see §13). Audience:
implementing agents. Builds on project 2's board, code and lessons
(`../../project-2/docs/design.md`).

## 0. Kyle's brief (2026-11-01)

- Phase 2 video outro, Kyle's words: "Next time, we'll make it wireless,
  connect it to the calendar, and talk about how the software works."
- "It's the finished product. I don't expect we need to move anything around
  on the breadboard."
- Decisions (2026-11-01):
  - Calendar: build against his **personal Google calendar**, switch to the
    work one later. "this is not my work laptop but i can have it finish that
    step after", so the work switch happens on the work laptop, by Kyle.
  - **The Mac app reads the calendar** (not the Pico, not a homelab helper).
  - **No hardware changes** (no snooze button).
  - Alert scheme approved: green = free, amber ≈ 5 min before, red + melody
    at the start, dim red during the meeting.
- Audience for the video: his sons, 7 and 9.
- Settings (2026-11-03, firmware 0.4.0): a colour per calendar status with
  "None = no state change"; overall volume and brightness "multiplied by the
  knob controls, so if volume is set to 50% and the knob is at 80%, i get
  40%"; an optional warning beep N minutes before; the start-of-meeting
  sound can be turned off.

## 1. Verified facts (2026-11-01)

| Fact | Value | Source |
|---|---|---|
| Wi-Fi stack | `pico_cyw43_arch_lwip_poll` (or `_threadsafe_background`); `cyw43_arch_init_with_country`, `cyw43_arch_enable_sta_mode`, non-blocking `cyw43_arch_wifi_connect_async(ssid, pw, auth)` | pico-sdk 2.3.1 `pico/cyw43_arch.h:191,473` |
| mDNS responder | `pico_lwip_mdns` library (lwIP `apps/mdns`) | pico-sdk 2.3.1 `pico_lwip/CMakeLists.txt:213` |
| Flash writes | `flash_safe_execute(func, param, timeout_ms)` (pauses the other core / IRQs safely) + `flash_range_erase/program` | pico-sdk 2.3.1 `pico/flash.h:87` |
| Calendar on macOS | EventKit. `-[EKEventStore requestFullAccessToEventsWithCompletion:]` (macOS 14+); Info.plist `NSCalendarsFullAccessUsageDescription`; completion runs on an arbitrary queue; after first grant call `reset` on a store that was created before the grant. Sees every account in System Settings → Internet Accounts (Google, Workspace, Exchange) | [Apple: requestFullAccessToEvents](https://developer.apple.com/documentation/eventkit/ekeventstore/requestfullaccesstoevents(completion:)), [TN3153](https://developer.apple.com/documentation/technotes/tn3153-adopting-api-changes-for-eventkit-in-ios-macos-and-watchos) |
| Local Network privacy | Apps touching the LAN need `NSLocalNetworkUsageDescription`; Bonjour browsing also `NSBonjourServices` (`_rma._tcp`). Permission is tracked by code signature, so **ad-hoc signing can behave unpredictably** (each rebuild may look like a new app). Shells inside tmux are blocked from the LAN on this Mac (memory: macos-tmux-local-network-block) | [TN3179](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy), [Eclectic Light 2026-10-22](https://eclecticlight.co/2026/10/22/local-network-privacy-is-getting-better/) |
| Board | Unchanged from project 2 (RGB GP10/11/12, buzzer GP15, knob GP26). The Pico 2 W's onboard LED is on the CYW43 chip (`CYW43_WL_GPIO_LED_PIN`), free to use as a Wi-Fi status light | project-2 design §3; pico-sdk |
| Power | No battery in the kit (inventory). "Wireless" = no data cable: the Pico runs from any USB phone charger | docs/inventory.md |

| lwIP + mDNS | lwIP 2.2.1; `mdns_resp_init/add_netif/add_service/announce` (`lwip/apps/mdns.h:108-122`). `mdns.c` refuses to build without `LWIP_IGMP=1`, `LWIP_UDP`, `LWIP_RAND`; also needs `LWIP_MDNS_RESPONDER=1`, `LWIP_NUM_NETIF_CLIENT_DATA≥1`, more `MEMP_NUM_SYS_TIMEOUT`. The SDK ships no Pico W `lwipopts.h`; we write our own from pico-examples' `lwipopts_examples_common.h` plus those | pico-sdk 2.3.1 `lib/lwip/src/apps/mdns/mdns.c:78-87` |
| Router drop | `CYW43_EV_DISASSOC` sets the link down and `wifi_join_state = 0`, so status reads `CYW43_LINK_DOWN`, not a failure code; the driver never rejoins by itself | `lib/cyw43-driver/src/cyw43_ctrl.c:353-355` |
| CYW43 start-up | `cyw43_arch_init` + `enable_sta_mode` block for hundreds of ms (fixed delays + firmware download over SPI) | `cyw43_ctrl.c:166-168`, `cyw43_ll.c` |
| Flash on one core | `flash_safe_execute` just disables interrupts; a 4 KB sector erase is ~45 ms typical, up to ~400 ms worst case (Winbond-class datasheets) | `pico_flash/flash.c:196-203` |

Built without `dns_sd.h`: the Mac finds the robot by resolving `rma.local` with `getaddrinfo` (macOS answers `.local` names through mDNS) on a background thread (§6).

## 2. Architecture

```
 Mac (awake, same Wi-Fi)                         Pico 2 W (USB charger)
 ┌──────────────────────────┐   TCP :4242     ┌───────────────────────────┐
 │ EventKit → next meetings │  line protocol  │ schedule (≤ 8 meetings,    │
 │ (times only, no titles)  │ ─────────────▶  │ relative times)            │
 │ Bonjour finds rma.local  │                 │ alert state machine        │
 │ Wi-Fi setup over USB     │ ◀── state ───── │ RGB, buzzer, knob (as p2)  │
 └──────────────────────────┘                 └───────────────────────────┘
```

- **The Mac reads, the robot keeps time.** Every 30 s (and on any calendar
  change notification) the app sends the next meetings as *seconds from now*.
  The Pico turns those into its own millisecond clock and runs the alert
  stages itself. So a Wi-Fi hiccup or the Mac dozing for a minute never
  makes an alert late, and the Pico needs no wall clock, NTP or time zone.
- **Privacy:** only start/end offsets cross the network. No titles, people
  or calendar names leave the Mac. (Also a good line for the video.)
- **Two transports, one protocol.** USB serial (as project 2) and TCP over
  Wi-Fi carry the same ASCII line protocol into the same `Device`. USB stays
  for flashing, Wi-Fi setup and debugging.
- Code layout: `project-3/` is a copy of `project-2/` (self-contained per
  project, as before), namespace `rma3`, protocol v3, firmware 0.3.0 (0.4.0: §3.2 settings).

## 3. Behaviour

### 3.1 Modes
- **Calendar** (default once a calendar is picked): the device chooses the
  colour and when to play.
- **Manual**: exactly project 2 (light toggle, colour, sound, play).
- The knob keeps its project 2 job in both modes (meter None / Brightness /
  Volume / Both). Volume 0 silences alerts too (decided in project 2).

### 3.2 Alert stages (Calendar mode), evaluated on the device every loop

| Stage | When | Light | Sound |
|---|---|---|---|
| Free | no meeting within 5 min | green, 40 % | — |
| Soon | 5 min ≥ time to next start > 0 | amber (255,120,0), 100 %, slow breathe (2 s period) | — |
| Start | at a start | red, 100 % | the selected sound, once (Off = silent) |
| In meeting | start ≤ now < end | red, 25 % | — |
| No schedule | Calendar mode but no schedule received yet, or schedule expired (§3.3) | dim blue, 15 % | — |

- Percentages multiply with the knob in Brightness/Both mode.
- **Configurable (0.4.0):** each look (No schedule, Free, Soon, Start, In
  meeting) has a colour, defaults as above with the level folded into the
  colour (green 40 % = 0,102,0, exact because levels are perceptual). A look
  set to *none* keeps whatever was showing (at boot: dark). Soon still
  breathes. Overall brightness and volume (0–100 %) multiply the knob when it
  controls them, in both modes.
- **Warning beep (0.4.0, off by default):** the Beep sound N minutes (1–60)
  before each start, once per meeting (its own id ring), same 60 s late
  rule, never after the start, and never cutting into another meeting's
  melody (skipped, not queued). The start melody can be switched off on its
  own (`ONSTART 0`).
- **Back-to-back:** in-meeting beats soon; a meeting starting while another
  is running still plays the melody once at its start.
- **Overlapping meetings:** in meeting until the last overlapping one ends.
- **Melody fires once per meeting**, tracked by an `alerted` flag keyed on a
  meeting **id** (16-bit, made by the Mac from a hash of the event identifier
  and start time; no title). Refreshes carry flags over by id, so delivery
  delays or rounding never re-trigger it. Up to 16 recently alerted ids are
  remembered even after their meeting leaves the schedule.
- **One late rule:** the melody plays if the device first sees the start
  within 60 s after it happened (a reboot or reconnect just before a meeting
  still alerts); a meeting that started longer ago is silent (no surprise
  melody when the app starts mid-meeting). The schedule lives in RAM, so a
  reset loses it until the Mac pushes again (it pushes immediately on
  connect).

### 3.3 Which events count (Mac side, pure function, tested)
Included: events in the chosen calendars in the next 12 h that are not
all-day, not cancelled, not declined by Kyle (`EKParticipantStatusDeclined`
on the self attendee), and not marked "free" (`EKEventAvailabilityFree`).
At most 8 in total: running meetings first, then upcoming earliest first. Starts earlier than 24 h ago are clamped to −86400 s (multi-day events). The Mac treats any error reply to a schedule line as a schedule failure (logged, retried on the next refresh), never as a reason to drop the connection.

**Staleness:** each schedule push carries a validity of 15 min. If the device
hears nothing for 15 min it drops to No schedule (dim blue) rather than
alerting from an old calendar. The app refreshes every 30 s, so this only
happens when the Mac is asleep, off, or away. This is the "Mac must be awake"
trade-off Kyle accepted, made visible instead of silent.

### 3.4 Connection behaviour
- Each transport has its **own line buffer** (USB, TCP), so bytes from one
  can never complete a line started on the other.
- One network client at a time; a new TCP connection replaces the old one
  (so the app on the work laptop can take over without a reboot). Run the
  app on one Mac at a time; documented.
- Client disconnect (USB DTR drop or TCP close) stops a manual preview, as
  project 2. It never stops a calendar alert melody and never clears the
  schedule (staleness handles that).
- Onboard LED: blinking = joining Wi-Fi, solid = on Wi-Fi, off = no Wi-Fi
  settings stored.
- **Wi-Fi supervisor** (pure, tested): any status other than up while
  credentials are stored (link down after a router drop, no IP, failure codes)
  for more than 10 s starts a fresh join; back-off 10 s → 20 s → 60 s cap,
  reset on success. After every rejoin, mDNS is re-announced
  (`mdns_resp_announce`).
- **Boot defaults:** Calendar mode, Westminster, meter None, light on. The
  app re-sends its own settings on connect (desired-state reconcile, as
  project 2); only Wi-Fi credentials are stored on the Pico.

## 4. Protocol v3 (`project-3/shared/rma3`)

ASCII lines as project 2, one reply per line, never unsolicited.
`kMaxLine` grows from 32 to 96 for the Wi-Fi commands. `HELLO` → `RMA 3 0.4.0`.

Unchanged from v2: `LIGHT`, `RGB`, `SOUND`, `PLAY`, `PLAY n`, `STOP`,
`METER`, `STATE`.

| New command | Effect |
|---|---|
| `MODE 0\|1` | 0 Manual, 1 Calendar |
| `CAL` | start a new schedule: staging cleared, and **now** taken as the time base for the following `EV` lines |
| `EV id s e` | append to staging: id 1–65535 (decimal), start and end in seconds from the `CAL` time base (−86400…86400, s < e, ≤ 8 entries) |
| `CALGO v` | commit staging as the schedule, valid for v seconds (60…3600); `alerted` flags carried over by id |
| `NET` | reply `OK NET W<0-3> <ip or ->`, Wi-Fi status 0 none-stored / 1 joining / 2 up / 3 failed |
| `WIFI <ssid>` | **USB only.** Stage SSID (rest of line, 1–32 bytes, spaces allowed) |
| `PASS <password>` | **USB only.** Stage password (rest of line, 8–63 bytes, or empty for open networks) |
| `WIFIGO` | **USB only.** Save staged SSID + password to flash and (re)join |
| `WIFICLR` | **USB only.** Erase stored Wi-Fi settings |
| `KEY <32 hex>` | **USB only.** Set the robot's key (saved to flash only if it changed) |
| `HI <32 hex nm>` | Network: reply `OK HI <nr> <robot proof>`. One per connection |
| `AUTH <32 hex>` | Network: the Mac's proof. `OK AUTH`, else `ERR AUTH`. One try per connection |
| `VOL n`, `BRIGHT n` | Overall volume / brightness, 0–100 % (0.4.0) |
| `WARN m` | Warning beep m minutes before each meeting, 1–60; 0 = off (0.4.0) |
| `ONSTART 0\|1` | Play the alert sound when a meeting starts (0.4.0) |
| `COL i R G B`, `COL i -` | Colour for look i (0 no schedule, 1 free, 2 soon, 3 start, 4 in meeting); `-` = none (0.4.0) |
| `CFG` | Reply `OK CFG V<vol> B<bright> W<warn> S<onstart>` + five `RRGGBB`/`-` in look order (0.4.0) |

**Settings line (0.4.0).** The new settings don't fit the state line, so
setters still reply with the state line and the app reads them with `CFG`:
once on connect, and again after sending any. Firmware that answers `CFG`
with an error is never sent them (the app says to `make flash`). Like every
other setting they live in RAM and the app puts them back on connect.

**Passphrase (added 2026-11-01).** Kyle: "can we add a password for the wifi
control?", then "i'd really rather you be able to take control over wifi
entirely if you know a passphrase". Each Mac derives the 16-byte key from the
passphrase with PBKDF2-HMAC-SHA256 (200,000 rounds, fixed salt) and stores
only the key (`~/Library/Application Support/RobotMeetingAlert/pairing.key`,
0600, shared with `rmactl`). The robot stores the key (flash record v2; v1
still read), set over USB with `KEY`. Any Mac with the passphrase can connect;
nothing is automatic, so one Mac never silently overwrites another's.

Handshake (mutual, so neither side is a signing oracle for an impostor):
Mac sends `HI nm`; robot answers `nr` and HMAC(key, "rma3 robot"‖nm‖nr); the
Mac checks it before sending HMAC(key, "rma3 mac"‖nm‖nr). Both derive a
session key HMAC(key, "rma3 session"‖nm‖nr). Every later Mac line ends in
` #<16 hex>` = HMAC(session key, counter‖line), counter +1 per accepted line,
so lines can't be forged, altered or replayed (a relay can pass traffic but
not change it). Replies are not signed (they only inform the app's display).
Until the handshake is done a network client gets only `HELLO`, `HI`, `AUTH`
(else `ERR AUTH`, or `ERR NOKEY` if the robot has no key). A new TCP
connection can't replace a trusted client that sent data in the last 5 s; an
untrusted one is dropped after 5 s. Crypto in `shared/rma3/auth.cpp`, tested
against FIPS 180, RFC 4231 and PBKDF2 vectors. Security review found the
first version (robot challenge only) relayable by a fake robot; fixed by the
mutual handshake and line tags. A second finding: any LAN peer can ask for a
robot proof (`HI`) and test passphrase guesses offline (as can anyone who
records a real handshake). Mitigations: the app's Generate button (20 chars
from 31 symbols, about 99 bits) is the recommended path; `HI` is limited to
one per second (`ERR BUSY`); PBKDF2 200k rounds. A PAKE (SPAKE2/CPace) was
considered and rejected as too heavy for the Pico for this threat. Changing
the key stays USB-only (sending a new key over Wi-Fi would need encryption).

State line adds mode, stage and Wi-Fi: `OK L1 CFF8000 S2 M1 K57 Z1 T1 A3 W2`
(T mode, A stage 0 none / 1 free / 2 soon / 3 meeting, W Wi-Fi status).
Worst case 36 chars; `kMaxReply` = 48; a test round-trips it.

Errors: v2's plus `ERR USBONLY` (Wi-Fi command over TCP) and `ERR FULL`
(9th `EV`), `ERR NOCAL` (`EV`/`CALGO` without `CAL`). The password is never echoed, logged, or included in any reply,
state line or debug print. `NET` reports only status and IP, not the SSID.

## 5. Firmware (`project-3/firmware`)

- Link `pico_cyw43_arch_lwip_poll pico_lwip_mdns pico_flash hardware_flash`
  plus project 2's libs. Our own `lwipopts.h` (pico-examples common set +
  the mDNS options in §1; no TLS, no DHCP server). Country code from `PICO_CYW43_ARCH_DEFAULT_COUNTRY_CODE` = CA.
- **Main loop** = project 2's loop plus `cyw43_arch_poll()` every iteration.
  **Start-up order:** buzzer pin low, LEDs off, `cyw43_arch_init` and STA
  mode (blocking, done **before** the watchdog is enabled; always, even with
  no credentials, so `WIFIGO` never starts the chip inside the loop), then
  watchdog 500 ms. In the loop nothing blocks longer than ~100 ms except the
  rare USB-only flash write (below): Wi-Fi join is async; status checked with `cyw43_tcpip_link_status` each loop; retry
  every 10 s on failure with no blocking.
- **TCP server** (lwIP raw API, port 4242): one client; bytes fed into the
  same `Device` through `feed(Source::Net, c, …)`, which has its own line
  buffer (USB uses `Source::Usb`); replies written with
  `tcp_write` + `tcp_output`; a 512-byte receive cap per poll keeps the loop
  bounded.
- **mDNS:** hostname `rma`, service `_rma._tcp` port 4242, started when the
  link comes up.
- **Flash record** (last 4 KB sector): magic, version, SSID, password, CRC32.
  Written **only** by the USB-only `WIFIGO`/`WIFICLR`, so nothing from the
  network can ever erase flash. Before the write: buzzer silenced, watchdog
  re-armed at 2 s, `flash_safe_execute`; afterwards back to 500 ms. Pure
  encode/decode in `shared/rma3/wifi_record.cpp` (tested). No other settings
  are persisted (boot defaults, §3.4).
- `Device::tick(now, host_connected)` changes: `host_connected` = USB **or**
  TCP client; stops only manual previews (§3.4).
- Alert state machine and schedule live in `shared/rma3/schedule.cpp`, pure,
  with injected time and 32-bit wrap-safe comparisons.

## 6. Mac side (`project-3/mac`)

- **Transports:** project 2's `SerialPort` gains `open_tcp` (non-blocking
  connect, 700 ms timeout, `SO_NOSIGPIPE`); the same line read/write code
  serves both. The link uses **USB when the robot is plugged in** (that is
  when Wi-Fi setup happens), otherwise Wi-Fi.
- **Discovery:** `RobotFinder` resolves `rma.local` every 5 s on a
  background thread (the link never waits on a lookup) and keeps the last
  good address; a manual address field in the app is tried too. The robot
  still advertises `_rma._tcp` for Bonjour browsers.
- **Calendar** (`calendar.mm`, Objective-C++, the only Apple-specific file):
  request full access, list calendars (title, account, colour), fetch events
  for the chosen calendars, subscribe to `EKEventStoreChangedNotification`.
  It hands plain structs (`start`, `end`, `all_day`, `declined`,
  `free`, `cancelled`) to a pure C++ `pick_meetings(now, events)` (tested).
- **App window** (Dear ImGui), sections top to bottom:
  1. Status: "Connected over Wi-Fi (192.168.x.y)" / "over USB" / "Searching".
  2. Mode: Calendar / Manual.
  3. Calendar: checkbox list of calendars grouped by account; "Next: 2:30 PM
     (in 12 min)"; **Test alert** button (pushes a fake meeting starting in
     10 s, 30 s long, then restores the real schedule).
  4. Manual controls (project 2's, enabled in Manual mode).
  5. Sound (alert melody) and Knob controls (both modes).
  6. Wi-Fi setup (collapsed; enabled only on USB): network name, password,
     Save. Shows the robot's Wi-Fi status and IP.
- Info.plist: `NSCalendarsFullAccessUsageDescription`,
  `NSLocalNetworkUsageDescription`, `NSBonjourServices = [_rma._tcp]`.
  Bundle id `com.kylep.robot-meeting-alert.project3`. Ad-hoc signed; if the
  Local Network prompt misbehaves across rebuilds, the doc tells Kyle where
  to toggle it (System Settings → Privacy & Security → Local Network).
  The build re-signs the whole bundle ad hoc with a fixed identifier so the
  Info.plist is sealed in. Deployment target macOS 14 (EventKit full access).
- App settings (chosen calendar ids, mode, last address) in
  `~/Library/Application Support/RobotMeetingAlert/project3.ini`. No secrets
  stored on the Mac side: the Wi-Fi password is typed once and sent over USB.
- **Quitting the app** leaves the robot as it is (project 2 turned the
  light off): in Calendar mode it keeps alerting from the schedule it has
  until that goes stale.
- **Link:** project 2's one-reply-per-line worker, extended with the schedule
  push (`CAL`, `EV`…, `CALGO` sent in order; an `ERR` reply aborts that push
  only) on connect, every 30 s, and on calendar change. Once a minute the app
  also asks the Calendar app to sync with its servers
  (`refreshSourcesIfNecessary`), since the Mac's local copy can keep
  deleted events for many minutes.
- **`rmactl` v3:** v2 commands plus `mode cal|manual`, `net`,
  `wifi` (prompts for SSID and password on stdin, never on the command
  line or in shell history), `wificlr`, `event IN_S DUR_S` (fake meeting, for
  hardware tests without a calendar), `--host ADDR` to use TCP instead of USB.
  `rmactl` has no calendar access (CLI tools get no EventKit prompt).
- **Work laptop handoff** (Kyle, later): clone, `make mac`, add work account
  in Internet Accounts, open the app, tick the work calendar, quit the app on
  the personal Mac. Documented as a short checklist in the README.

## 7. Security

- Repo is public: no SSIDs, passwords, IPs or calendar data in code, tests,
  docs or screenshots (secret scan before every commit, as always). Test
  fixtures use made-up values.
- Wi-Fi credentials go only over USB, into the Pico's flash. Network clients
  cannot read or change them (`ERR USBONLY`). Stored in plain text on the
  Pico (no secure storage on this board): documented; `WIFICLR` before
  giving the board away.
- The TCP port requires the passphrase (§4): mutual challenge-response and
  per-line signatures. Traffic is not encrypted (others on the LAN can read
  it, not forge it). A weak passphrase can be guessed offline from recorded
  traffic; PBKDF2 slows that down. Buzzer safety limits (≤ 50 % duty, watchdog) apply whatever
  the source.
- Line length caps, bounded drains, and no heap growth on the device keep a
  misbehaving client from wedging it.

## 8. Tests (host, no hardware)

- Protocol v3: every new command, ranges, `ERR FULL`, `ERR NOCAL`,
  `ERR USBONLY` by source, interleaved USB and TCP bytes (a partial TCP
  `WIFICLR` completed by a USB newline must not run as USB), SSID with spaces, 32/33-byte SSID, 7/8/63/64-byte password,
  password never appears in any reply, worst-case state line round-trip.
- Schedule/alerts with injected time: each stage boundary (5 min, start,
  end), melody exactly once, refresh with shifted offsets (±5 s) keeps
  `alerted` by id, first seen ≤ 60 s after start plays, > 60 s silent, back-to-back and overlapping, staleness → No schedule, 32-bit ms
  wrap (start near 0xFFFFFFFF), Volume 0 silence, Off silence.
- `pick_meetings`: all-day, declined, free, cancelled excluded; 12 h window;
  max 8 in total with running first; −86400 clamp.
- Wi-Fi supervisor: link down after up → rejoin after 10 s, back-off
  sequence, reset on success, no credentials → idle.
- Flash record encode/decode: CRC mismatch, wrong magic, empty → "none stored".
- Link over TCP: loopback server running the real `Device`; connect,
  reconcile, schedule push, reconnect replaces old client, Wi-Fi commands
  rejected over TCP.
- CI matrix extended to `project-3` (host + firmware).

## 9. Hardware acceptance (Kyle + agent)

1. `make flash` over USB; `rmactl wifi` (Kyle types the password); `rmactl net` → W2 + IP.
2. Unplug from the Mac, power from a phone charger. Onboard LED solid.
3. App (run from Finder, not tmux) finds the robot over Wi-Fi.
4. `Test alert`: amber breathing → red + melody at 10 s → dim red → green.
5. Real check: a personal-calendar event 7 min out; watch amber at 5 min,
   melody at the start.
6. Close the laptop lid for > 15 min: robot goes dim blue. Open it: green.
7. Power-cycle the robot: rejoins Wi-Fi, comes back in Calendar mode.

## 10. Video: "How the software works" (after hardware acceptance)

No assembly part (board unchanged). Same toolkit and format as phase 2:
section title cards, am_michael for the explainer, af_bella for the quiz,
quiz music bed, countdown quiz. Draft outline (≈ 8 min), to be sketched for
Kyle's approval before animating, as in phase 2:
1. Recap: last time the Mac was the boss over a cable; today the cable goes.
2. Wi-Fi is radio: messages as invisible light, the router as a post office,
   addresses (the robot's own address on the network).
3. How the Mac finds the robot: it shouts "is there a robot here?" (Bonjour).
4. Reading the calendar: the Mac looks, sends only "next meeting in 12
   minutes", never the meeting's name.
5. The robot keeps time itself: counting milliseconds, the stages as a
   flow chart (free → soon → start → in meeting → free).
6. How the code is organised: the same instructions run on the Mac and the
   robot (protocol), a loop that never stops, and tests that check the code
   without hardware. A few real lines on screen, big and highlighted.
7. Quiz (8 questions), "How many did you get right?", outro.

## 11. Task order (after Kyle approves)

1. Scaffold `project-3/` from project 2 (rename to rma3, versions, CI matrix).
2. `shared/rma3`: protocol v3, schedule/alert state machine, flash record, tests.
3. Firmware: Wi-Fi, TCP server, mDNS, flash; `make firmware && make size`.
4. Mac: transports, discovery, `rmactl` v3, link tests over loopback TCP.
5. Mac: EventKit, `pick_meetings` + tests, app UI; screenshot (dark mode).
6. Docs: README (setup, Wi-Fi, work-laptop checklist, troubleshooting), root README row.
7. Kyle flashes (no rewiring), agent runs §9 with him.
8. Video outline → Kyle approves → build → render → Kyle reviews (never
   published without his review).

## 12. Open questions (assumptions taken)

- Free = steady green all day and night in Calendar mode. A quiet-hours
  setting (light off outside, say, 8:00–18:00) is easy to add; not included.
- Amber window 5 min, as approved; a constant the app could expose later.
- Alert melody = the selected sound (project 2's selector). Off = silent alerts.
- Staleness 15 min (dim blue). Shorter is noisier when the laptop sleeps
  briefly; longer risks alerting for a meeting deleted while the Mac slept.
- One Mac at a time; the newest connection wins.

## 13. Review log (2026-11-01, opus, fresh context)

All eight findings checked against the SDK source / project 2 code and accepted:
1. Shared line buffer let a TCP prefix become a USB command → per-source buffers + test.
2. Settings persisted on network writes would wear flash and risk the credentials → only USB-only Wi-Fi commands write flash; boot defaults instead.
3. ±2 s start matching could double-play → meeting ids; offsets based at `CAL`.
4. Router drop reads as link down, not failure, and never rejoins → Wi-Fi supervisor + mDNS re-announce.
5. "8 plus running" could hit `ERR FULL` and drop the link → cap 8 total, clamp, schedule errors never disconnect.
6. No SDK `lwipopts.h`; mDNS needs extra options → own lwipopts with the listed options.
7. CYW43 start-up and flash erase block → init before the watchdog; 2 s watchdog around flash writes.
8. Contradictory late-start rules → single 60 s rule; reset loses schedule until the next push.
