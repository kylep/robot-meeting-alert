# Phase 1 design: USB-controlled R/G/B LEDs + hold-to-buzz

Status: REVIEWED + open questions resolved (7-model adversarial review 2026-10-31, findings merged — see §12).
Audience: implementing agents. Scope: Kyle's items 1 (macOS C++ app) and 2
(assembly guide). Item 3 (tutorial video) is unblocked (Kyle, 2026-10-31:
skill ready) but is built after `docs/assembly.md` exists, since it narrates it.

## 0. Verified facts (2026-10-31)

| Fact | Value | Source |
|---|---|---|
| Kit | Freenove FNK0064E = FNK0064 with Pico 2 W | Amazon order; Kyle |
| Kit docs (local) | `/Users/kp/gh/reports/Freenove_Basic_Starter_Kit_for_Raspberry_Pi_Pico-main/` — `C/C_Tutorial.pdf` byte-identical to GitHub copy (sha1 `daf3d1b0…`); has a Pico 2W section (p.13) | shasum |
| Board | Raspberry Pi Pico 2 W, RP2350A, SDK board id `pico2_w` (`pico2_w.h` sets no `PICO_DEFAULT_LED_PIN`; onboard LED is on CYW43 — unused) | pico-sdk 2.3.1 |
| Board USB connector | micro-USB B. Kit cable is USB-A → micro-B | kit parts image |
| Physical pins used | GP10=pin14, GP11=pin15, GP12=pin16, GP15=pin20, GND=pins 18 & 28, VBUS=pin40 | tutorial p.72 schematic |
| Pico SDK | 2.3.1 | GitHub releases |
| picotool | 2.3.1 (brew formula `picotool`, bottled). If `find_package(picotool)` fails the SDK fetches+builds it with `PICOTOOL_NO_LIBUSB=1` (tools/Findpicotool.cmake) — no libusb needed for the build | brew; SDK source |
| Arm toolchain | brew cask `gcc-arm-embedded` 15.3.rel2 is a `.pkg` → needs sudo → agent cannot use it unattended. Use the Arm GNU Toolchain darwin-arm64 `arm-none-eabi` tarball instead (§7). Formula `arm-none-eabi-gcc` lacks newlib — don't use | brew info |
| USB reset (SDK 2.3.1 `pico_usb_reset/usb_reset_config.h`) | `PICO_ENABLE_USB_RESET_VIA_BAUD_RATE` default 1 (1200 baud → BOOTSEL); `PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE` default 1 — this is what `picotool -f` uses. Old `PICO_STDIO_USB_*` names still honoured | SDK source |
| `stdio_usb_connected()` | returns `tud_cdc_connected()` = DTR (since `PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=0`) | stdio_usb.c |
| stdio_usb stdout | blocks up to `PICO_STDIO_USB_STDOUT_TIMEOUT_US` (default 500000) when TX FIFO full | stdio_usb.h/.c |
| Dear ImGui | v1.93.0; `examples/example_sdl3_sdlrenderer3/main.cpp`, backends `imgui_impl_sdl3`, `imgui_impl_sdlrenderer3` exist at tag | GitHub |
| SDL3 | `release-3.4.18` | GitHub |
| Freenove "C" tutorial | Arduino IDE (arduino-pico), NOT Pico SDK (`Start Here.pdf`). Reuse circuits only | kit docs |
| LED circuit | GPIO → 220Ω → LED anode (long leg); cathode → GND | tutorial p.35 |
| Buzzer circuit | active buzzer `+` → 5V (VBUS); `−` → S8050 collector; emitter → GND; GPIO → 1kΩ → base | tutorial p.71–72 |
| S8050 pinout | flat labelled face toward viewer, legs left→right E, B, C | tutorial p.71 |
| Kit breadboard lettering | CONFIRMED from Kyle's photo (IMG_2238, 2026-10-31): with column 1 at the LEFT, row **a is nearest the viewer** (a–e near half, f–j far half) — same as tutorial p.72 | photo |
| Kit breadboard size | CONFIRMED 830-point, 63 columns; rail colour lines continuous end to end. Near rail pair: red(+) inner (next to row a), blue(−) outer. Far rail pair: blue(−) inner (next to row j), red(+) outer | photo |
| Kit ships parts pre-inserted | LED bar graph pressed into the breadboard at ≈ cols 46–58, rows f–g (protective). Must be removed before assembly (`Start Here.pdf`: lift one end slightly, then the other) | photo; Start Here.pdf |
| Local machine | macOS 27.2, Apple clang 17, brew; cmake/ninja/arm gcc/picotool NOT installed | `command -v` |

Transport is **USB CDC serial**. "USB-C controlled" in Kyle's brief is read as
"controlled over the USB cable"; the board side is micro-USB (open question
Q1). No Wi-Fi, no CYW43, no credentials anywhere in phase 1.

## 1. Requirements

- R1. macOS app (C++) with three on/off switches Red, Green, Blue and one
  momentary "Hold to buzz" button; buzzer sounds only while held.
- R2. App finds the Pico automatically (Raspberry Pi USB VID only), shows
  connection state, survives unplug/replug without restart.
- R3. Firmware (C++, Pico SDK 2.3.1) drives 3 LEDs + active buzzer from serial commands.
- R4. Buzzer fail-safe: buzzer stops within ≤500 ms if the app's GUI thread
  hangs, the app crashes or quits, or the cable is pulled. LEDs are NOT
  fail-safed (they keep their last state across host disconnect; harmless,
  and lets one-shot CLI commands work).
- R5. `docs/assembly.md`: step-by-step breadboard assembly using only
  FNK0064E parts, with an authoritative netlist, an exact hole map for the
  kit's breadboard lettering, a mirror rule for reversed lettering,
  verification gates and troubleshooting.
- R6. Everything buildable/testable by an agent on Kyle's Mac without
  hardware, except final visual/audible acceptance.

Non-goals: Wi-Fi, calendar, menu-bar app, notarization, non-mac GUI, RGB LED,
PWM brightness, passive buzzer tones.

## 2. Repo layout

```
Makefile                   wrappers: test, mac, run, firmware, flash, size, clean
shared/rma/
  device.hpp/.cpp          device state machine; freestanding-friendly: no std::string,
                           no heap, no exceptions (linked into firmware AND host)
  host.hpp/.cpp            host helpers (std::string ok): format/parse replies (host only)
firmware/
  CMakeLists.txt
  pico_sdk_import.cmake    verbatim from pico-sdk 2.3.1 external/
  src/main.cpp             SDK glue only
  src/pins.hpp
mac/
  CMakeLists.txt           FetchContent SDL3 release-3.4.18, imgui v1.93.0; links IOKit, CoreFoundation
  src/serial_port.hpp/.cpp POSIX termios wrapper
  src/discovery.hpp/.cpp   IOKit enumeration of /dev/cu.* with USB idVendor 0x2E8A
  src/link.hpp/.cpp        Link: worker thread, handshake, reconcile, buzz freshness
  src/app_main.cpp         ImGui + SDL3 GUI → "Robot Meeting Alert.app"
  src/rmactl.cpp           CLI
tests/
  CMakeLists.txt           CTest; builds shared/ + mac/src/{serial_port,link}.cpp (not discovery, not GUI)
  check.hpp                CHECK macro + failure counter
  test_device.cpp
  test_host.cpp
  test_link.cpp            openpty()-backed fake device
docs/assembly.md
docs/design.md             (this file; was docs/designs/phase-1.md)
README.md                  build/flash/run, what worked for toolchain
.github/workflows/ci.yml
.gitignore                 build/
```

## 3. Wire protocol v1

ASCII lines. Host sends `\n`-terminated lines. Device output goes through SDK
CRLF translation → host strips trailing `\r`. Max inbound line 32 bytes
excl. terminator; longer → discard through next `\n`, reply `ERR TOOLONG`.
Leading/trailing spaces trimmed; empty line → no reply. Case-sensitive.

| Host → device | Effect | Reply |
|---|---|---|
| `HELLO` | none | `RMA 1 <fw_version>` (`0.1.0`) |
| `LED <R\|G\|B> <0\|1>` | set LED | `OK <state>` |
| `BUZZ 1` | buzzer on; lease deadline = now + 300 ms (re-send extends) | `OK <state>` |
| `BUZZ 0` | buzzer off, lease cleared | `OK <state>` |
| `ALLOFF` | all off | `OK <state>` |
| `STATE` | none | `OK <state>` |
| other | none | `ERR BADCMD` (unknown verb) / `ERR BADARG` (bad/missing args) |

`<state>` = `R<0|1> G<0|1> B<0|1> Z<0|1>`, e.g. `OK R1 G0 B0 Z1`. Exactly
one reply per non-empty line; never unsolicited output.

### 3.1 `shared/rma/device.hpp`

```cpp
namespace rma {
struct Outputs { bool r = false, g = false, b = false, buzz = false; };
class Device {
 public:
  static constexpr uint32_t kBuzzLeaseMs = 300;
  static constexpr size_t kMaxLine = 32;
  static constexpr size_t kMaxReply = 32;
  // Feed one byte. If it completes a non-empty line, writes NUL-terminated
  // reply (no newline) into out[kMaxReply] and returns true.
  bool feed(char c, uint32_t now_ms, char* out);
  // Expire lease; host_connected=false → buzzer off + lease cleared (LEDs kept).
  void tick(uint32_t now_ms, bool host_connected);
  Outputs outputs() const;
};
}
```
Lease compare wrap-safe: expired iff `(int32_t)(now - deadline) >= 0`.

### 3.2 `shared/rma/host.hpp`
`std::string format_state(const Outputs&)`;
`bool parse_state_reply(std::string_view, Outputs*)` (accepts `OK R? G? B? Z?` only);
`bool parse_hello_reply(std::string_view, std::string* fw)` (requires `RMA 1 `).

## 4. Firmware

- CMake: `cmake_minimum_required(VERSION 3.13)`; `set(PICO_BOARD pico2_w)`
  before `include(pico_sdk_import.cmake)`; `project(rma_firmware C CXX ASM)`;
  `CMAKE_CXX_STANDARD 17`; `pico_sdk_init()`. SDK source: `PICO_SDK_PATH` if
  set, else `-DPICO_SDK_FETCH_FROM_GIT=ON -DPICO_SDK_FETCH_FROM_GIT_TAG=2.3.1`.
- Target `rma_firmware`: `src/main.cpp ../shared/rma/device.cpp` (NOT host.cpp);
  link `pico_stdlib` only; `pico_enable_stdio_usb(rma_firmware 1)`;
  `pico_enable_stdio_uart(rma_firmware 0)`;
  `target_compile_definitions(rma_firmware PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000 RMA_FW_VERSION="0.1.0")`;
  `pico_add_extra_outputs(rma_firmware)`. Leave USB reset defaults (both on).
- `pins.hpp`: `kRed=10, kGreen=11, kBlue=12, kBuzzer=15`.
- `main()`: per pin `gpio_init`; `gpio_put(pin,0)`; `gpio_set_dir(pin,GPIO_OUT)`
  (no drive-strength change: current is set by the series resistor).
  `stdio_init_all()`. Loop:
  ```
  for (;;) {
    service(now());                       // tick + write GPIO
    for (int n = 0; n < 64; ++n) {        // bounded drain per iteration
      int c = getchar_timeout_us(0);
      if (c == PICO_ERROR_TIMEOUT) break;
      if (dev.feed((char)c, now(), reply)) {
        service(now());                   // apply BEFORE replying
        printf("%s\n", reply);            // may block ≤20 ms
        service(now());                   // re-check lease after possible block
      }
    }
    sleep_us(500);
  }
  service(t): dev.tick(t, stdio_usb_connected()); write dev.outputs() to the 4 GPIOs.
  now(): to_ms_since_boot(get_absolute_time()) — always fresh, never cached.
  ```
  Worst-case buzzer overrun past lease ≈ 20 ms (one blocked printf).
- Gate: `arm-none-eabi-size build/firmware/rma_firmware.elf` text+data < 256 KB
  (record actual in README). Do not gate on .uf2 size (~2× binary).

## 5. macOS side

### 5.1 SerialPort
- `open(path)`: `::open(O_RDWR|O_NOCTTY|O_NONBLOCK)`; `ioctl(TIOCEXCL)`
  (EBUSY → return `Busy` distinct from `NotFound`/`Error`); `tcgetattr`;
  `cfmakeraw`; `c_cflag |= CLOCAL|CREAD|HUPCL`; `cfsetspeed(115200)` —
  NEVER 1200 (BOOTSEL reset); `tcsetattr(TCSANOW)`; `tcflush(TCIOFLUSH)`.
- `write_all(data, deadline)`: loop handling short writes; `EAGAIN` →
  `poll(POLLOUT)` until deadline; `EINTR` → retry; other errno → Dead.
- `read_line(deadline)`: `poll(POLLIN)`; `read` > 0 → buffer, split on `\n`,
  strip `\r`; `read` == 0, `POLLHUP`, `POLLERR`, `ENXIO`, `EIO` → Dead;
  `EAGAIN`/`EINTR` → keep polling until deadline → Timeout.

### 5.2 Discovery (`discovery.cpp`, macOS only)
`std::vector<std::string> find_pico_ports()`: `IOServiceGetMatchingServices`
on `IOServiceMatching(kIOSerialBSDServiceValue)`; for each, read
`kIOCalloutDeviceKey` (`/dev/cu.*`); `IORegistryEntrySearchCFProperty(service,
kIOServicePlane, CFSTR("idVendor"), …, kIORegistryIterateRecursively |
kIORegistryIterateParents)`; keep where idVendor == 0x2E8A. Plain C API — no
Objective-C. Sorted. Unrelated CDC devices are never opened.

### 5.3 Link (worker thread owns the port exclusively)
- Ctor takes `std::function<std::vector<std::string>()> candidates` (default
  `find_pico_ports`; tests inject pty path) and a clock.
- States `Searching` → `Connected{port, fw}` → `Searching` on any error.
- Searching (scan every 1000 ms): per candidate: open (skip on Busy, record
  `last_error="port busy"`), `write_all("\nHELLO\n")`, read lines ≤500 ms
  until `parse_hello_reply` succeeds; else close and continue. Stray
  non-matching lines during handshake are ignored.
- On connect: send `ALLOFF` (deterministic start; app re-applies desired LEDs next).
- Connected loop (20 ms cadence):
  - `d = desired` snapshot; **buzz freshness**: effective_buzz =
    `d.buzz && (now − d.stamp) ≤ 150 ms`. (GUI stall ⇒ stamp ages ⇒ host
    stops refreshing ⇒ device lease expires ≤300 ms later.)
  - each LED with `d.x != reported.x` → `LED X v`.
  - effective_buzz && (!reported.buzz || ≥100 ms since last `BUZZ 1`) → `BUZZ 1`.
  - !effective_buzz && reported.buzz → `BUZZ 0`.
  - no command sent in the last 500 ms → `STATE` (liveness).
  - every command: `write_all` then await one reply ≤500 ms → `parse_state_reply`
    → `reported`. Dead/Timeout/ERR/parse fail → close, `Searching`.
- API (thread-safe): `set_desired(Outputs)` (stamps time); `snapshot()` →
  `{state, port, fw, reported, last_error}`; `stop()`: sets atomic flag;
  worker sends `ALLOFF` best-effort, closes, exits; caller joins. Only the
  worker ever touches the fd.

### 5.4 GUI (`app_main.cpp`)
- Base on imgui v1.93.0 `examples/example_sdl3_sdlrenderer3/main.cpp`.
  Window "Robot Meeting Alert", 380×300 logical, `SDL_WINDOW_HIGH_PIXEL_DENSITY`,
  content-scale handling as in the example.
- Call `link.set_desired()` once per loop iteration **before** the example's
  minimized-window `SDL_Delay(10); continue;` branch, with buzz forced false
  when minimized or unfocused (`SDL_EVENT_WINDOW_FOCUS_LOST`).
- Single borderless full-window panel:
  1. Status: green dot `Connected · <port> · fw <ver>`; grey dot
     `Looking for Pico… (plug in USB)`; amber `Port busy — close other serial apps`.
  2. Three toggle switches Red/Green/Blue (channel-tinted), always enabled,
     drive `desired`; small dot per row shows `reported`.
  3. Full-width 64 px button "Hold to buzz"; `buzz = ImGui::IsItemActive()`.
- On `SDL_EVENT_QUIT`: `link.stop()` then teardown.
- CMake: `add_executable(rma_app MACOSX_BUNDLE …)`, `OUTPUT_NAME "Robot Meeting Alert"`,
  bundle id `com.kylep.robot-meeting-alert`; SDL3 static (`SDL_SHARED OFF`,
  `SDL_STATIC ON`); `-framework IOKit -framework CoreFoundation`. No sandbox,
  no entitlements (`/dev/cu.*` is accessible to non-sandboxed apps).

### 5.5 `rmactl`
`rmactl [--port PATH] <hello|state|alloff|led r|g|b on|off|buzz MS>`.
Discovers via `find_pico_ports()` unless `--port`. LED state persists after
exit (R4). `buzz MS`: `BUZZ 1` every 100 ms for MS then `BUZZ 0`. Prints reply
lines. Exit: 0 ok; 1 ERR/timeout; 2 no device; 3 port busy ("quit Robot
Meeting Alert first").

## 6. Tests (host; `make test`; no third-party framework)

- `test_device`: every command happy path; case (`led r 1` → BADCMD);
  BADARG (`LED X 1`, `LED R 2`, `BUZZ`, `LED R`); 33-byte line → TOOLONG then
  next line parses; `STATE\r\n` tolerated; whitespace trim; empty line no
  reply; lease on at t, on at t+299, off at t+300; re-BUZZ at t+200 → off at
  t+500; wrap at `UINT32_MAX−100`; `tick(t,false)` → buzz off, LEDs unchanged;
  exact reply strings.
- `test_host`: `parse_state_reply`/`parse_hello_reply` accept valid, reject
  malformed/partial/`RMA 2 …`.
- `test_link` (pty; set master raw): fake thread feeds `rma::Device` and
  replies `…\r\n`. Assert:
  1. Connected ≤2 s, fw `0.1.0`;
  2. `set_desired({1,0,1,0})` → fake outputs match ≤200 ms;
  3. buzz held via `set_desired` every 16 ms for 600 ms → fake Z=1 throughout, ≥4 `BUZZ 1`;
  4. release → Z0 ≤200 ms;
  5. **GUI stall**: `set_desired(buzz=1)` once then stop calling → fake Z0 ≤500 ms (device lease or explicit BUZZ 0);
  6. fake stops replying → Searching ≤1.5 s;
  7. fake answers HELLO with `ERR BADCMD` → never Connected;
  8. `stop()` → fake received `ALLOFF`, thread joined.
- Discovery is not unit-tested (IOKit); covered by hardware acceptance.

## 7. Toolchain, build, flash

Toolchain (agent, no sudo):
1. `brew install cmake ninja picotool`.
2. Arm GNU Toolchain: from developer.arm.com download the latest
   `arm-gnu-toolchain-<ver>-darwin-arm64-arm-none-eabi.tar.xz` (verify
   version + sha256 from the page at implementation time), extract to
   `~/.local/arm-gnu-toolchain/`, pass `-DPICO_TOOLCHAIN_PATH=<dir>` (Makefile
   variable `PICO_TOOLCHAIN_PATH`, overridable). If Gatekeeper quarantines the
   binaries: `xattr -dr com.apple.quarantine <dir>`.
3. Record exact versions in README.

Makefile:
- `test`: `cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ctest --test-dir build/tests --output-on-failure`
- `mac`: `cmake -S mac -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/mac`
- `run`: `open "build/mac/Robot Meeting Alert.app"`
- `firmware`: `cmake -S firmware -B build/firmware -G Ninja -DPICO_SDK_FETCH_FROM_GIT=ON -DPICO_SDK_FETCH_FROM_GIT_TAG=2.3.1 $(if $(PICO_TOOLCHAIN_PATH),-DPICO_TOOLCHAIN_PATH=$(PICO_TOOLCHAIN_PATH)) && cmake --build build/firmware`
- `size`: `arm-none-eabi-size build/firmware/rma_firmware.elf`
- `flash`: `picotool load -x -f build/firmware/rma_firmware.uf2`
  (first flash: Kyle holds BOOTSEL while plugging in → `RP2350` drive; later
  flashes: `-f` resets via the vendor interface; the app must not hold the port).

## 8. CI (`.github/workflows/ci.yml`)

- `host` (macos-latest): `brew install ninja`; `make test`; `make mac`.
- `firmware` (ubuntu-latest): apt `cmake ninja-build gcc-arm-none-eabi
  libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`; `make firmware`;
  `make size`; upload `rma_firmware.uf2`.
- Before every push run locally: `make test mac firmware` (repo CLAUDE.md +
  global rule). Push only green.

## 9. Assembly guide (`docs/assembly.md`) — content spec

### 9.1 Coordinate system (gate before any part goes in)
Board on the desk, long side toward Kyle, column numbers increasing
left→right. Kit lettering (per tutorial p.72): row **A nearest Kyle**; near
half A–E, far half F–J; each 5-hole column-half is one node; rail pairs run
along the near edge (below A) and far edge (above J). Identify rails by
printed colour line, red `+`, blue `−`, never by position.

Gate G0 (Kyle confirms, guide tells him how): (a) letters: A is the row
closest to you; (b) the board has ≥ 60 numbered columns; (c) are the rail
lines continuous or broken mid-board? If (a) fails (A is on the far side),
use the **mirror rule**: swap letters a↔j, b↔i, c↔h, d↔g, e↔f and
"near rails"↔"far rails" in every instruction; physical positions are
identical. If (b) fails, stop and send Kyle's breadboard photo to the agent
for a re-layout. If (c) rails are split, add bridges: far red(+) (col 1 ↔
col 59), far blue(−) (13 ↔ 54), near blue(−) (18 ↔ 29/36/43) — one jumper per
split rail across the gap.

### 9.2 Content (in order)
0. Before you start: cable check — kit cable is USB-A → micro-B; Kyle has a
   suitable cable for his Mac (confirmed). Remove the pre-inserted LED bar
   graph (≈ cols 46–58) and anything else pressed into the board (lift one
   end slightly, then the other). Then Gate G0 (expected to pass: lettering
   and size confirmed from Kyle's photo; still have Kyle eyeball it).
1. Parts pick list (from FNK0064E): Pico 2 W, USB cable, breadboard,
   3× 220Ω, 1× 1kΩ, 1 red + 1 green + 1 blue LED, 1 active buzzer, 1 S8050
   (marked 8050, NOT 8550), 12 jumper wires. Identification: resistor bands
   (5-band 220Ω red-red-black-black-brown; 1kΩ brown-black-black-brown-brown;
   4-band variants red-red-brown-gold / brown-black-red-gold; kit colour-code
   card); LED long leg = `+` anode; active buzzer = sealed underside + "REMOVE
   SEAL AFTER WASHING" sticker (keep sticker on for now; peel later for
   louder); buzzer `+` marked on top, longer leg.
2. Safety: USB unplugged while wiring. VBUS is 5V straight from the Mac —
   it must never touch any blue rail/GND. Nothing goes in the 3V3 pins.
3. Place the Pico (top side up, USB connector pointing LEFT, off the board's
   left end). Straddle the channel so the pin rows land in **c** (near) and
   **h** (far) — 2 free holes per column on each side. (Freenove's drawing
   uses b/g; we use c/h deliberately.) Mapping: pins 1–20 → row c, pin n at
   column n; pins 21–40 → row h, pin p at column 41−p. Gate G1: printed
   label next to h1 reads VBUS, c1 reads GP0/0, c20 reads GP15/15, c18 GND, h13 GND.
4. Power wiring:
   - W1 j1 → far red(+) rail  (VBUS 5V)
   - W2 j13 → far blue(−) rail (GND, pin 28 — NOT pin 38 at h3: j2 next to it
     is VSYS, diode-tied to VBUS, so a one-hole slip would short USB 5V to GND)
   - W3 a18 → near blue(−) rail (GND, pin 18)
5. LEDs (near half):
   | LED | GPIO (pin, hole) | jumper | 220Ω | LED long(+) / short(−) | ground jumper |
   |---|---|---|---|---|---|
   | Red | GP10 (14, c14) | W4 a14→a24 | R1 b24→b28 | e28 / e29 | W5 a29→near blue(−) |
   | Green | GP11 (15, c15) | W6 b15→a31 | R2 b31→b35 | e35 / e36 | W7 a36→near blue(−) |
   | Blue | GP12 (16, c16) | W8 a16→a38 | R3 b38→b42 | e42 / e43 | W9 a43→near blue(−) |
6. Buzzer (far half):
   - W10 b20 → g50 (GP15 across the channel)
   - R4 1kΩ i50 → i55
   - Q1 S8050 legs in row j: E j54, B j55, C j56 — flat labelled face toward
     you (toward the channel). Sanity: reading the flat face, legs are E,B,C
     left→right and E sits in the lower-numbered column.
   - W11 h54 → far blue(−)
   - Buzzer `−` f56, `+` f59 (body overhangs the centre channel)
   - W12 j59 → far red(+)
   Clearance (reviewed): buzzer body (Ø12 mm centred on row f between cols
   56–59) clears Q1 in row j, R4 in row i, W11 at h54, W12 at j59.
7. Netlist (authoritative; table above is one realisation):
   - 5V: VBUS(40) – far red(+) – buzzer `+`
   - GND: pin28 – far blue(−); pin18 – near blue(−); Q1 E; 3× LED cathodes
   - GP10(14) – R1 – red anode; GP11(15) – R2 – green anode; GP12(16) – R3 – blue anode
   - GP15(20) – R4 – Q1 B; Q1 C – buzzer `−`
   Agent writing the guide MUST cross-check table ↔ netlist ↔ ASCII diagram
   ↔ `pins.hpp` and state that it did.
8. Pre-power checklist: re-read every W/R/part hole; far red(+) rail holds
   only W1 and W12; no lead touches a neighbour.
9. First flash: hold BOOTSEL, plug in, release; `RP2350` drive appears; agent
   runs `make flash` (or drag `rma_firmware.uf2` onto the drive). After boot:
   nothing lit, silent.
10. Smoke test (app closed): `rmactl hello` → `RMA 1 0.1.0`; `rmactl led r on`
    → red stays lit; same for g/b; `rmactl buzz 500` → 0.5 s beep;
    `rmactl alloff`.
11. App: `make run`; toggle each switch; hold the button.
12. Brightness note: blue/green at 3.3V through 220Ω run ~1–2 mA — visibly
    lit but dimmer than red; that's expected.
13. Troubleshooting table: LED never lights (LED reversed; wrong column;
    W3/W5/W7/W9 missing; rails split); LED always on (wired to a power pin);
    buzzer silent (passive buzzer used — no sticker/open underside; 8550 used;
    Q1 rotated; W1/W12 missing; buzzer reversed); buzzer always on (C/E
    swapped; collector shorted to GND); buzzer sounds while in BOOTSEL
    (RP2350-E9 pad latch possible if flashed while buzzing — unplug/replug);
    app "Looking for Pico…" (charge-only cable; not flashed; still in BOOTSEL);
    "Port busy" (quit other serial apps; `lsof /dev/cu.usbmodem*`).
Include an ASCII hole-map diagram generated from the same table.

## 10. Task breakdown (dependency-ordered; commit + push after each green step)

1. Toolchain (§7). Gate: `arm-none-eabi-gcc --version`, `picotool version`, `cmake --version`.
2. `shared/rma/*` + `test_device`, `test_host` (TDD). Gate: `make test`.
3. `firmware/`. Gate: `make firmware && make size`.
4. `serial_port`, `link` + `test_link`. Gate: `make test` (all 8 link cases).
5. `discovery` + `rmactl`. Gate: `rmactl hello` with nothing attached → exit 2, clear message.
6. GUI. Gate: `make mac`; launch; dark-mode screenshot showing "Looking for Pico…"; quit cleanly ≤1 s.
7. `docs/assembly.md` + ASCII map. Gate: fresh-context reviewer subagent cross-checks §9.2.7 items; zero mismatches.
8. README, `.gitignore`, CI. Gate: `make test mac firmware` green locally, then push, then CI green.
9. Hardware acceptance (Kyle present): cable + G0/G1 gates → assemble → BOOTSEL flash → agent runs §9.2.10 while Kyle confirms each LED and beep → app toggles → hold button → (a) unplug while buzzing → silent; (b) quit while buzzing → silent; (c) `kill -STOP` the app while buzzing → silent ≤0.5 s, then `kill -CONT`; (d) unplug/replug → app reconnects and re-applies switches.

## 11. Open questions / risks

- Q1. RESOLVED: USB cable control; Kyle has a suitable cable.
- Q2. RESOLVED: item 2 ends "fire the buzzer while the button is held" (Kyle confirmed).
- Q3. RESOLVED: breadboard lettering (a nearest) and 63 columns confirmed by photo; G0 + mirror rule kept as a cheap check.
- Risk: RP2350-E9 — if the board is reset into BOOTSEL while GP15 is high, the pad may latch ~2 V and keep the buzzer on until power-cycle. Low probability (only flashing mid-buzz); documented in troubleshooting. No hardware mitigation in phase 1; revisit (external pull-down on GP15) if it is ever observed.
- Risk: macOS DTR-on-close behaviour unverified on this hardware; R4 does not depend on it (lease + buzz freshness cover quit/crash).

## 12. Review log (2026-10-31)

Reviewers: fable, opus, sonnet, haiku (Claude); gpt-6-astra, gpt-6.1-sol, gpt-6-luna (Codex). All completed.

Accepted (fixed above):
- GUI-thread hang kept buzzer on via stale desired state (all 7 raised) → buzz freshness stamp (§5.3) + test 5 + acceptance (c).
- Breadboard lettering handedness: kit prints A nearest viewer; draft assumed opposite → would have tied 5V to GP0 (fable; confirmed against tutorial p.72) → §9.1 coordinate system, gates, mirror rule, re-derived map.
- One-shot `rmactl led r on` turned off on exit via DTR all-off (opus, astra, sol) → disconnect clears buzzer only (R4, §3.1).
- Firmware loop: printf blocking ≤500 ms and stale timestamp delayed lease (astra, sol, sonnet, haiku) → bounded drain, fresh `now()`, service around replies, stdout timeout 20 ms.
- Discovery probed every `/dev/cu.usbmodem*` (opus, fable, sonnet, haiku) → IOKit VID 0x2E8A filter.
- Idle liveness 2 s vs 1 s test (astra, sol, fable) → probe after 500 ms idle, test ≤1.5 s.
- EAGAIN/short-write/read()==0 handling (astra, opus) → §5.1.
- `stop()` cross-thread write race (fable) → worker-only fd.
- Host helpers with std::string linked into firmware (fable) → device/host split.
- picotool needs no libusb; reset macro renamed; vendor interface is what `-f` uses (fable, sonnet partially) → §0, §4.
- cask is .pkg/sudo → tarball primary (fable) → §7.
- 13 vs 12 jumpers; dangling sentence; port-busy exit code; rails-split bridges count; cable check as step 0; size gate measures ELF (opus, fable, sonnet) → fixed.
- Blue/green dimness; drive-strength no-op (sonnet, haiku) → note added, drive-strength removed.
- "USB-C" reinterpretation unlisted (haiku) → Q1.
- Buzzer body clearance (haiku) → checked by fable/opus (~2.5–4 mm); clearance note in §9.2.6.

Rejected:
- luna: "GP10 is pin 15, GP15 is pin 21, GND is pin 19" — wrong; kit schematic (tutorial p.72) and Pico pinout: pin 14 = GP10, pin 20 = GP15, pin 18 = GND.
- sonnet: "SDK defaults reset-via-baud to 0" — SDK 2.3.1 `usb_reset_config.h` defaults it to 1 (vendor interface also 1).

Deferred: base pull-down for BOOTSEL/E9 (sonnet, low) — see §11 risk.

Guide cross-check (opus, fresh context, 2026-10-31): wiring, map, S8050
orientation and rails all PASS. Fixed: far-side ground moved from pin 38 (h3)
to pin 28 (h13) because j2 (VSYS) sat between W1 and W2; G0 column-count and
rail-split checks added to the guide; LED-bar location wording; cross-check
statement added.
