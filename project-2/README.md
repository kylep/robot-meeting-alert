# Project 2: RGB light, melodies and a knob

Levels up [project 1](../project-1/): the three single LEDs become one
**RGB LED** that can show any colour at any brightness, the beeper becomes a
**passive buzzer** that plays melodies, and a **knob** (potentiometer) can
dial the brightness, the volume, both, or neither.

- **Firmware:** C++ on the Pico SDK 2.3.1 (`firmware/`): PWM for colour and
  tones, ADC for the knob, a watchdog for safety.
- **Mac app:** Dear ImGui + SDL3 (`mac/`): light on/off, colour wheel, sound
  picker with Play, knob mode, live knob reading.
- **Protocol v2:** plain-text lines over USB serial (`shared/rma2/`).

Docs:
- [Assembly guide](docs/assembly.md): turn the project 1 board into project 2
- [Design](docs/design.md): wiring, protocol, firmware details, review log
- [Kit inventory](../docs/inventory.md)

## Quick start

Run these from this `project-2/` folder (toolchain setup: see
[project 1](../project-1/README.md#toolchain-macos-apple-silicon)).

```sh
make test       # protocol, colour, melody, knob and serial-link tests
make mac        # "build/mac/Robot Meeting Alert.app" and build/mac/rmactl
make firmware   # build/firmware/rma2_firmware.uf2
make flash      # silences the buzzer, then loads the firmware
make run        # opens the app
```

## Sounds

All public domain, about 3 seconds, played two octaves up where the small
buzzer is loudest:

| # | Sound | Notes |
|---|-------|-------|
| 1 | Westminster chime (Big Ben) | G♯ F♯ E B, E G♯ F♯ B |
| 2 | Twinkle twinkle | C C G G A A G |
| 3 | Ode to joy | E E F G G F E D |
| 4 | Beep | 2 kHz, 0.3 s |

## rmactl

```
rmactl [--port PATH] hello | state | light on|off | rgb R G B | sound N
       | play [N] | stop | meter none|bright|vol|both | bal R G B | watch SECONDS
```

`play` waits for the sound to finish (closing the port stops it). `watch`
prints the knob reading as it changes. `bal` tunes the white balance live
(not saved). Exit codes: 0 ok, 1 device error, 2 no Pico, 3 port busy,
64 usage.

## Layout

```
shared/rma2/  device state machine, melodies, sequencer, knob filter, host helpers
firmware/     Pico SDK project (PWM LED + buzzer, ADC knob, watchdog)
mac/          app, rmactl, serial port, USB discovery, Link worker
tests/        host tests (CTest), including a pty-backed fake device
docs/         assembly guide, design
```
