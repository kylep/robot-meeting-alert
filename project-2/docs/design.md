# Project 2 design: RGB light, melodies, and a knob

Status: DRAFT (reviewed once, fixes merged; see §11), awaiting Kyle's approval before any build. Audience: implementing
agents. Builds on project 1's board, code and lessons (`../../project-1/docs/design.md`).

## 0. Kyle's brief (2026-10-31, verbatim intent)

Hardware: RGB LED with RGB values; passive buzzer playing a cute ~3 s melody
(found online); potentiometer whose value can control brightness, volume, both
or neither. Software: on/off for the light; colour picker; melody selector
(Off + 3 melodies + Beep); meter setting (None / Brightness / Volume / Both);
meter reading (display only). One merged video (assembly + how it works), but
render **only the assembly part first**; the how-it-works part comes after
Kyle confirms the hardware works. No software video yet (deep-dive later).
Teaching audience: Kyle's sons, 7 and 9. Drop phase-1 basics; level up.

## 1. Verified facts (2026-10-31)

| Fact | Value | Source |
|---|---|---|
| RGB LED | 4 legs; left→right (legs toward you, flat/short side left): **R, common (longest), G, B**. **Common anode**: common leg to +3.3 V; a colour lights when its leg is pulled **low**. 220 Ω per colour | Freenove C tutorial ch.5, p.63–64 |
| Passive buzzer | Same circuit as phase 1 (5 V, S8050 low side, 1 kΩ base, GP15); driven with PWM at the note frequency | tutorial ch.6.2, p.75–77 |
| Potentiometer | Rotary, 10 kΩ, legs **1, 3 (wiper, middle), 2**; 1→3.3 V, 2→GND, 3→ADC | tutorial ch.8, p.89–90 |
| ADC | ADC0 = GP26 (pin 31). RP2350 ADC is 12-bit (the Arduino tutorial reads 10-bit). `adc_gpio_init` sets NULL function, disables pulls and input buffer | pico-sdk 2.3.1 `hardware/adc.h` |
| PWM slices | gpio < 32 → slice `(gpio>>1)&7`, channel `gpio&1`. GP10→5A, GP11→5B, GP12→6A, GP15→7B. GP14 (7A) must stay unused because it shares the buzzer's slice, whose frequency changes per note. GP13 (6B) shares the blue LED's 2.3 kHz slice and is free for future use | pico-sdk 2.3.1 `hardware/pwm.h` |
| Far-side pins (row h, column 41−p) | 3V3 pin 36 = h5; **3V3_EN pin 37 = h4 (never touch: grounding it kills 3.3 V)**; AGND 33 = h8; GP26 31 = h10; **RUN 30 = h11 (never touch: grounding it resets)**; GND 28 = h13 | Pico pinout |
| Melodies (public domain) | Westminster Quarters changes 1–2: G♯ F♯ E B / E G♯ F♯ B. Twinkle Twinkle: C C G G A A G. Ode to Joy: E E F G G F E D | [Wikipedia: Westminster Quarters](https://en.wikipedia.org/wiki/Westminster_Quarters); singing-bell.com Twinkle chart; Mutopia (Ode to Joy, public domain) |

Melody note values were not all independently verified online (Ode to Joy is
standard-opening-from-memory); Kyle should confirm by ear at acceptance.

## 2. Requirements

- P1. RGB LED shows any colour (r, g, b 0–255) at any brightness, smooth
  (gamma-corrected), no visible flicker.
- P2. Light on/off independent of colour (off keeps the chosen colour).
- P3. Sounds: Off, Westminster chime, Twinkle Twinkle, Ode to Joy (each ≈3 s),
  Beep (0.3 s). Played on demand; never loops; stops on host disconnect.
- P4. Knob reading 0–100 %, stable (no jitter), shown live in the app.
- P5. Meter mode None / Brightness / Volume / Both. Brightness mode: knob
  scales LED brightness live, on the device (no app round-trip). Volume mode:
  knob scales buzzer loudness live. Otherwise full brightness / full volume.
- P6. Mac app: light toggle, colour picker, sound selector + Play, meter mode,
  meter reading bar, connection status. Same auto-discovery and reconnect as
  project 1.
- P7. Everything except final look/listen testable without hardware.
- P8. Assembly guide + video part 1 (assembly), one step at a time, safety
  scene, starting from a bare breadboard with only the Pico.

Non-goals: Wi-Fi, calendar integration, persistence of settings across power
cycles, custom user melodies.

## 3. Hardware layout (rebuilt from the bare board, 2026-10-31)

Kyle's revision: the video starts from a breadboard with only the Pico and
builds everything in place (no removal steps). Layout goals, not narrated:
the RGB LED sits toward the right end with no wire crossing it; resistors
stand across the centre channel; jumpers are long, so wires run along the
board edges (near-half wires sag toward the viewer, far-half wires toward
the far rails) and no wire passes over a part. The circuit and pins are
unchanged from §1, so the firmware is unchanged.

The step-by-step holes live in [assembly.md](assembly.md) §2–5. They are
generated from the video script
(`claude-video/projects/phase2/video.json`), which is the source of truth
because Kyle builds from the video. Placement order: power, knob, light,
buzzer. The RGB LED is turned around (legs B, G, common, R left to right,
longest leg third) so Wires 7–9 nest without crossing. Near rails unused.

Netlist (authoritative; verified by a script that unions the breadboard
strips through every wire and checks each part's legs):
- 5 V: VBUS(40, h1) – Wire 1 – far + rail – Wire 11 – buzzer + (f60) – diode band (i60)
- 3.3 V: 3V3(36, h5) – Wire 3 – knob right leg (f30) – Wire 6 – RGB common (g49)
- GND: pin 28 (h13) – Wire 2 – far − rail – {Wire 5 – knob left leg (f26), Wire 10 – S8050 E (j55)}
- GP10(14) – Wire 7 – 220 Ω (e50/f50) – RGB R (g50); GP11(15) – Wire 8 – 220 Ω (e48/f48) – RGB G (g48); GP12(16) – Wire 9 – 220 Ω (e47/f47) – RGB B (g47)
- GP26(31, h10) – Wire 4 – knob wiper (f28)
- GP15(20, c20) – Wire 12 – 1 kΩ (e56/f56) – S8050 B (j56); S8050 C (j57) – buzzer − (f57) – diode plain end (i57)
- Untouched: 3V3_EN (h4), RUN (h11), VSYS (h2).

Electrical checks:
- RGB current per colour ≈ (3.3 − Vf)/220: red ≈ 6 mA, green/blue ≈ 1–2 mA,
  sunk by the GPIO. Within RP2350 limits.
- Knob: 10 kΩ across 3.3 V draws 0.33 mA; wiper into a high-Z ADC input.
- **Buzzer coil current:** the passive buzzer is a low-resistance coil
  (~16–42 Ω). Under PWM it sees AC-like pulses; if GP15 were ever held high
  constantly, the S8050 could pass hundreds of mA and heat it. Mitigations:
  firmware never sets duty above 50 %; a hardware watchdog (500 ms) resets a
  hung firmware (reset releases GP15, and the 1 kΩ + base pull it low);
  `make flash` sends `STOP` before rebooting; troubleshooting + safety scene
  say "buzzer hums or gets warm → unplug".
- At boot, before the firmware runs, GPIO pads default to pull-down, so the
  RGB colours may glow faintly until `main()` drives them high (off). Harmless.
- The resistors standing across the channel (f→e) are the Freenove pattern
  (tutorial p.64). Adjacent ones at columns 47 and 48 sit close together:
  bend them slightly apart so the bare leads don't touch.

Physical risks / gates:
- G-POT: the kit knob's legs are 2 holes apart (5 mm; confirmed in the kit drawing). If they fit
  adjacent holes, use f26/f27/f28: Wire 4 → j27, Wire 3 → i28, Wire 6 from j28, Wire 5 stays j26.
- Knob direction: if turning right lowers the value, swap the outer legs' connections (3.3 V and ground).
  The firmware does not care.
- The knob body overhangs the channel toward the viewer; Wires 7–9 pass in
  front of it along row a. Fine: long jumpers arc over.
- G-RGB: the longest leg must be in g49, the single short (R) leg in g50. If the legs have been trimmed, the
  flat edge on the rim is next to the R leg (Freenove drawing).

## 4. Protocol v2 (`project-2/shared/rma2`)

ASCII lines as project 1 (≤ 32 chars in, CRLF out, one reply per line,
never unsolicited). `HELLO` → `RMA 2 0.2.0`.

| Command | Effect |
|---|---|
| `LIGHT 0\|1` | light off/on (colour kept) |
| `RGB r g b` | colour, each 0–255 |
| `SOUND n` | select sound: 0 Off, 1 Westminster, 2 Twinkle, 3 Ode, 4 Beep. Never starts or stops playback |
| `PLAY` | play the selected sound once (restarts if already playing; no-op for Off) |
| `PLAY n` | play sound n once without changing the selection (previews) |
| `STOP` | stop sound |
| `METER n` | 0 None, 1 Brightness, 2 Volume, 3 Both |
| `STATE` | no change |

Every successful command replies with the state line:
`OK L1 CFF8000 S2 M1 K57 Z1` (light, colour as hex RRGGBB, sound, meter
mode, knob % 0–100, playing). Worst case `OK L1 CFFFFFF S4 M3 K100 Z1` = 27
chars; device reply buffer `kMaxReply = 40` (project 1's 32 would truncate a
32-char line: its copy keeps 31). A test asserts the worst case round-trips.
Errors: `ERR BADCMD|BADARG|TOOLONG`. Reconnect never plays anything (the app
reconciles SOUND, which never starts playback).

Disconnect (DTR drop): stop sound; light/colour/mode persist (as project 1).

## 5. Firmware (`project-2/firmware`)

- Same SDK/toolchain/CMake setup as project 1 (`PICO_SDK_FETCH_FROM_GIT_TAG:STRING=2.3.1`,
  board `pico2_w`, stdio USB, `PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000`).
  Link `pico_stdlib hardware_pwm hardware_adc`.
- **LED PWM**: GP10/11/12 → slices 5/5/6, wrap 65535 (≈2.3 kHz at 150 MHz,
  invisible flicker), `pwm_set_output_polarity` inverted on those channels so
  level 65535 = fully on (common anode). Level = round(65535 × scale[ch] ×
  (c/255)^2.2 × b^2.2), b = knob/100 (modes 1, 3) else 1.0; 0 when light off.
  `scale` is per-channel white balance (start red 0.30, green 1.0, blue 1.0;
  tune by eye at acceptance so `RGB 255 255 255` looks white). Inversion
  (common anode) is done by `pwm_set_output_polarity`, tested separately. Init
  order: set level 0 (pin high = off) before enabling PWM.
- **Buzzer PWM**: GP15 slice 7 B. Per note f: **integer** clkdiv
  `div = ceil(sys_clk / (65536·f))`, `wrap = round(sys_clk/(div·f)) − 1`
  (fractional dividers add jitter). Change notes during the inter-note rest
  (level 0): clkdiv applies immediately, wrap is double-buffered. Level =
  wrap × duty, duty = 0.5 × vol^0.7 from a small table (perceptual), with
  0 → silent (vol = knob% in modes 2, 3 else 1.0). Duty never exceeds 50 %. Rest/stop = level 0. Notes are
  played two octaves above the written melody (≈1–2 kHz), where the small
  buzzer is loud; ~25 ms gap between notes so repeated notes are distinct.
- **Melodies** (pure data in `shared/rma2/melodies.cpp`, ms):
  1. Westminster: G♯6 F♯6 E6 B5(long), E6 G♯6 F♯6 B5(long) — 3×300 + 600 per change ≈ 3.0 s.
  2. Twinkle: C6 C6 G6 G6 A6 A6 375 ms each, G6 750 ms ≈ 3.0 s.
  3. Ode to Joy: E6 E6 F6 G6 G6 F6 E6 330 ms each, D6 690 ms ≈ 3.0 s.
  4. Beep: 2000 Hz, 300 ms.
- **Knob**: ADC0 every 10 ms; exponential moving average; dead zones of
  60 counts at each end (≤ 60 → 0 %, ≥ 4035 → 100 %, linear between);
  ±1 % hysteresis in the middle only, so 0 and 100 are always reachable.
- **Watchdog**: `watchdog_enable(500, true)`, fed every loop iteration.
- **Loop**: as project 1 (bounded input drain, fresh `now()`), plus
  `sequencer.tick(now)` and `knob.tick(now)` every iteration; output levels
  recomputed whenever state or knob changes.
- Pure logic (device state machine, sequencer, gamma/levels, knob filter) lives
  in `shared/rma2/*.cpp` with no SDK dependencies; firmware is thin glue.

## 6. Mac side (`project-2/mac`)

Starts from project 1's app (copied, not shared: each project stays
self-contained for teaching).
- Link: desired-state reconcile for LIGHT/RGB/SOUND/METER; PLAY/STOP are
  queued one-shot events; `STATE` poll every 100 ms for the knob reading.
- GUI (Dear ImGui): status line; **Light** toggle; **Colour** `ColorPicker3`
  (wheel) + preset swatches (red, green, blue, yellow, purple, white); **Sound**
  combo (Off, Westminster chime, Twinkle twinkle, Ode to joy, Beep) + **Play**
  button. Choosing a sound sends `SOUND n` then `PLAY n` (preview); Play
  sends `PLAY`. Commands are sent in order on the single worker queue; **Knob controls** radio
  None / Brightness / Volume / Both; **Knob** read-only progress bar with %.
  Window ≈ 420×560.
- `rmactl` v2: `hello | state | light on|off | rgb R G B | sound N | play | stop
  | meter none|bright|vol|both | watch SECONDS` (prints knob % every 100 ms:
  lets the agent verify the knob while Kyle turns it). `play` holds the port
  open and polls `STATE` until `Z0` (timeout 4 s); otherwise closing the port
  (DTR drop) would stop the melody immediately.

## 7. Tests (host, no hardware)

- Protocol: every command, BADARG ranges (RGB 256, SOUND 5, METER 4, PLAY 5),
  state line format, worst-case reply length round-trip.
- Levels: gamma table endpoints, inversion, brightness and light-off, mode
  matrix (None/Brightness/Volume/Both × knob 0/50/100).
- Sequencer: injected time; each melody's total length within ±50 ms of 3 s;
  correct note at sampled times; gaps between repeated notes; STOP and
  disconnect silence; PLAY restarts.
- Knob filter: noise ±20 counts produces no % change; a sweep from 40 to
  4060 counts (real pot ends) reaches exactly 0 and 100.
- Link (pty fake running the real device code): connect, reconcile, PLAY
  event delivered once, STATE poll updates knob reading, drop → Searching.
- CI: matrix over `project-1` and `project-2` (host + firmware jobs each).

## 8. Assembly guide + video (part 1 only)

- `project-2/docs/assembly.md`: starts from the working phase-1 board.
  Sections: what changes; remove (one item per step); swap buzzer; add (one
  item per step, table above); safety checks; flash; test with `rmactl`; app.
  Same coordinate conventions, gates and troubleshooting style as project 1.
  Hole map generated from the same table; independent cross-check by a
  fresh-context reviewer (as project 1).
- Video `phase2` (claude-video project, local only until Kyle reviews): one
  step per cue with "Wire N is colour. It goes from X to Y." wording; removals
  shown as parts lifting out; safety scene; no chat-only asks; `[[pause]]`
  where the viewer is asked something. Part 2 (how it works: PWM/brightness,
  RGB mixing, tones and frequency, the knob as a voltage divider and ADC)
  appended only after Kyle confirms the build works.
- Toolkit additions (claude-video): breadboard item kinds `rgbled` (4 legs,
  clear dome tinted by glow colour), `pot` (3 legs, knob body, rotatable for
  part 2), `remove` (lifts a placed part out; later scenes drop it), buzzer
  `variant: passive`. Diagram components for part 2: `rgbled`, `pot`.

## 9. Task order (after Kyle approves)

1. `project-2/` scaffold from project 1 (Makefile with `flash` = `rmactl stop || true` then picotool; CMake; CI matrix).
2. `shared/rma2` + tests (protocol, levels, sequencer, melodies, knob).
3. Firmware; `make firmware && make size`.
4. Mac link + app + `rmactl` v2; link tests; screenshot.
5. `docs/assembly.md` + hole map; fresh-context wiring review.
6. Toolkit additions + phase 2 video part 1 (assembly) → render → Kyle reviews.
7. Kyle rewires + flashes; agent runs `rmactl` checks (colours, each sound,
   knob `watch`), Kyle confirms by eye/ear; app run.
8. After Kyle's go: video part 2 (how it works) appended; render.

## 10. Open questions (assumptions taken)

- Sound selector semantics: choosing a sound previews it once, **Play** replays
  it. Off means silence. (The future meeting-alert trigger will play the selected sound.)
- Westminster, Twinkle and Ode to Joy are the three melodies (all public domain,
  recognisable, ~3 s). Easy to swap: they are data tables.
- Knob at 0 in Volume/Both mode = silent, including alerts. Natural for a
  volume knob; Kyle to confirm (alternative: a minimum audible floor).
- Volume = PWM duty cycle (0–50 %). True loudness control is coarse on a
  piezo/magnetic buzzer; the knob will still go from loud to quiet to silent.

## 11. Review log (2026-10-31, opus, fresh context)

Confirmed correct: hole map ↔ table ↔ netlist (no collisions; nothing near
3V3_EN/RUN; no interference at cols 50–59), RGB pin order, PWM slices for
GP10/11/12/15, melody note sequences and 3.0 s totals, E9 not applicable.

Accepted and fixed above: reply could hit 32 chars and truncate in project
1's buffer (hex colour + kMaxReply 40); passive-buzzer coil DC risk (watchdog,
duty cap, STOP before flash, troubleshooting); flyback diode (A13); `rmactl
play` cut off by DTR drop; colour balance + unambiguous gamma/brightness;
knob dead zones; PLAY n / SOUND semantics and ordering; GP13 rationale;
integer buzzer clkdiv; perceptual volume curve; knob-over-Wire-10 note.
Assembly-guide cross-check (opus, fresh context): all nets, map, RGB order,
diode orientation and pins PASS. Fixed: knob ends mirrored to match the kit
(right = more); two troubleshooting rows; i55/i56 adjacency check; `make mac
flash`. The assembly guide uses distinct map symbols.
