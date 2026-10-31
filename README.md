# robot-meeting-alert

Control a Raspberry Pi Pico 2 W over USB from a small macOS app: three LEDs
(red, green, blue) you can switch on and off, and a buzzer that sounds while
you hold a button. Phase 1 of a desk "meeting alert" robot built from the
Freenove FNK0064E starter kit.

- **Firmware:** C++ on the official Raspberry Pi Pico SDK 2.3.1 (`firmware/`)
- **Mac app:** C++ with Dear ImGui v1.93.0 + SDL3 3.4.18 (`mac/`)
- **Shared protocol:** a plain-text line protocol over USB serial (`shared/rma/`)

Docs:
- [Assembly guide](docs/assembly.md): wire up the breadboard
- [Phase 1 design](docs/designs/phase-1.md): protocol, safety rules, review log
- [Kit inventory](docs/inventory.md)

## Quick start

```sh
make test       # protocol + serial-link tests (no hardware needed)
make mac        # builds "build/mac/Robot Meeting Alert.app" and build/mac/rmactl
make firmware   # builds build/firmware/rma_firmware.uf2
make flash      # loads the firmware (first time: hold BOOTSEL while plugging in)
make run        # opens the app
```

Then follow the [assembly guide](docs/assembly.md).

## Toolchain (macOS, Apple silicon)

Tested on macOS 27.2 with:

| Tool | Version | Install |
|------|---------|---------|
| CMake | 4.4.5 | `brew install cmake` |
| Ninja | 1.13.2 | `brew install ninja` |
| picotool | 2.3.1 | `brew install picotool` |
| Arm GNU Toolchain | 15.3.Rel2 (`arm-none-eabi-gcc` 15.3.1) | tarball, see below |
| Apple clang | 17 | Xcode command line tools |

The Homebrew cask `gcc-arm-embedded` is a `.pkg` installer that needs sudo,
so use Arm's tarball instead:

```sh
V=15.3.rel2
F=arm-gnu-toolchain-$V-darwin-arm64-arm-none-eabi.tar.xz
U=https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/$V
curl -LO "$U/$F" && curl -LO "$U/$F.sha256asc" && shasum -a 256 -c "$F.sha256asc"
mkdir -p ~/.local/arm-gnu-toolchain && tar -xJf "$F" -C ~/.local/arm-gnu-toolchain
```

The Makefile picks it up from `~/.local/arm-gnu-toolchain/` automatically.
Override with `make firmware PICO_TOOLCHAIN_PATH=/path/to/toolchain`.

Notes:
- The Pico SDK is fetched at build time (tag 2.3.1). The tag is passed as
  `-DPICO_SDK_FETCH_FROM_GIT_TAG:STRING=…`, because the SDK's import script
  declares it as a file path and CMake 4 would otherwise expand it into one.
- SDL3 and Dear ImGui are fetched by CMake on the first `make mac`, which
  takes a few minutes.
- Firmware size (`make size`): 27.7 KB text, 2.7 KB bss.

## rmactl

A command-line tool for testing the hardware without the app (quit the app
first; only one program can hold the port):

```
rmactl [--port PATH] hello | state | alloff | led r|g|b on|off | buzz MS
```

Exit codes: 0 ok, 1 device error or timeout, 2 no Pico found, 3 port busy,
64 usage.

## Layout

```
shared/rma/   protocol state machine (device.*) + host helpers (host.*)
firmware/     Pico SDK project
mac/          app, rmactl, serial port, USB discovery, Link worker
tests/        host tests (CTest), including a pty-backed fake device
docs/         assembly guide, design, inventory
```
