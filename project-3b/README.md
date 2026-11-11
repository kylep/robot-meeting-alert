# Project 3b: the meeting alert on Windows (experimental)

> **Experimental, untested on real Windows.** This was written and
> cross-compiled on a Mac. GitHub's Windows build machine compiles it and
> runs the logic tests, but nobody has yet run the app on a Windows PC, with
> Outlook, or with the robot plugged into Windows. Expect rough edges, and
> please report what happens.

It's the [project 3](../project-3/) app for a Windows PC. The robot and its
firmware are the same, and so are the lights, sounds and settings. The
difference is the calendar: this app reads **Outlook** on the PC instead of
the Mac's Calendar app. As on the Mac, **only meeting times go to the robot,
never titles or people.**

## Which calendar source?

In the app, Settings → Calendar source:

| You use | Choose |
|---|---|
| **Classic Outlook** (the one in Microsoft 365 / Office, with File and Options menus) | **Outlook (classic)**. It reads the calendars Outlook already has. Outlook must be installed; the app starts it in the background if it isn't open |
| **New Outlook for Windows**, Outlook on the web, or anything else | **iCal link (.ics)**. The new Outlook has no way for other apps to read it, so publish your calendar as a link instead (below) |

**Publishing an iCal link** (Microsoft 365 or Outlook.com): in Outlook on the
web, Settings → Calendar → Shared calendars → Publish a calendar. Pick your
calendar and "Can view when I'm busy", press Publish, and copy the **ICS**
link. If "Publish a calendar" is missing, your IT department has turned it
off. Then in the app: Settings → Calendar source → iCal link → paste → **Save
link**. The app downloads it every 5 minutes. Treat the link like a password
(anyone with it can see your calendar): the app keeps it only in your own
Windows user folder. To cancel a leaked link, unpublish and publish again.

Either way, tick the calendar(s) on the Robot tab. Declined, cancelled,
all-day and "free" meetings are skipped.

## Getting the app

**Option A, download it.** Each change to this repo builds
`RobotMeetingAlert.exe` and `rmactl.exe` on GitHub: open the repo's
**Actions** tab → the latest green **ci** run → **project-3b-windows** under
Artifacts (needs a GitHub account), and unzip it. The files aren't signed, so
Windows may say "Windows protected your PC": click **More info → Run anyway**
only if you got the file from this repo.

**Option B, build it** (about 15 minutes the first time):

1. Install [Git](https://git-scm.com/download/win),
   [CMake](https://cmake.org/download/) and
   [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/)
   with the **Desktop development with C++** workload.
2. Open **Developer PowerShell for VS** and run:

   ```powershell
   git clone https://github.com/kylep/robot-meeting-alert.git
   cd robot-meeting-alert\project-3b
   cmake -S . -B build -A x64
   cmake --build build --config Release
   ctest --test-dir build -C Release      # logic tests, no robot needed
   ```

   The programs are `build\Release\RobotMeetingAlert.exe` and
   `build\Release\rmactl.exe`.

With [MSYS2](https://www.msys2.org/) (UCRT64 shell) instead of Visual Studio,
this should also work (not tried): `pacman -S --needed git
mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake
mingw-w64-ucrt-x86_64-ninja`, then `cmake -S . -B build -G Ninja
-DCMAKE_BUILD_TYPE=Release` and `cmake --build build`.

From a Mac or Linux (this is how it was checked), with mingw-w64 installed:

```sh
cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build/win
```

## First-time setup

1. **Put the firmware on the robot** (once). Windows needs no tools for this:
   unplug the robot, hold the **BOOTSEL** button on the Pico while plugging it
   into the PC, and a drive called **RP2350** opens. Copy
   `rma3_firmware.uf2` onto it; the Pico restarts by itself. Get the file from
   the same ci run's **project-3-firmware** artifact, or build it on a Mac
   (`make firmware` in `project-3`). A robot already set up from a Mac can
   skip this.
2. **Wi-Fi**, with the robot still on USB. In a terminal in the folder with
   `rmactl.exe`: `.\rmactl wifi` (asks for the network name and password; the
   password isn't shown or saved on the PC). Then `.\rmactl net` until it
   says `W2` and an address. 2.4 GHz networks only. Or use **Wi-Fi setup** in
   the app.
3. **Passphrase**, still on USB: in the app, Settings → Robot passphrase →
   **Generate** → write it down → **Save, and set on robot**. Or
   `.\rmactl pair`. If the robot was already paired with a Mac, just type
   the same passphrase and **Save on this PC** (no USB needed).
4. Unplug the robot and power it from any USB charger.
5. Run `RobotMeetingAlert.exe`, choose **Calendar** mode, pick the calendar
   source (above), tick your calendar, and press **Test alert**: amber now,
   the melody in 10 seconds, red for 30 seconds, then green.

Only one computer should drive the robot at a time. `rmactl` works as in
[project 3](../project-3/README.md#rmactl); on Windows its `--port` takes a
name like `COM5` (usually not needed: it finds the robot by itself).

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Classic Outlook isn't installed" | You have the new Outlook (or none). Use the iCal link source. If you have classic Outlook too, switch the "New Outlook" toggle off in Outlook and try again |
| "Couldn't reach Outlook" | Outlook or this app was started with "Run as administrator". Start both normally |
| "Outlook isn't answering" | Outlook is closed, still starting, or showing a dialog. The app tries again every 30 seconds |
| A Microsoft 365 meeting shows up late | Classic Outlook must be online and synced; the app sees what Outlook sees |
| App says "Looking for the robot" | Same Wi-Fi as the PC? Windows may not find `rma.local` on some networks (for example ones marked "Public"): type the robot's address (from `rmactl net`) under Settings → Robot address |
| "port busy" | Close `rmactl` or any other serial program (Arduino IDE, PuTTY) |
| Robot isn't found over USB | Check Device Manager → Ports (COM & LPT) for a "USB Serial Device" while the robot is plugged in. Use a data USB cable, not a charge-only one |
| iCal link "couldn't download" | Check the link in a browser. Company networks sometimes block it |

Other problems, sounds and the light settings: see
[project 3's README](../project-3/README.md).

## How it differs from the Mac app

The window, robot connection, schedule logic, iCal reader and `rmactl` are
project 3's own code, built from `../project-3`. Only the parts that talk to
the operating system are new, in `src/`:

| File | Mac version | Windows version |
|---|---|---|
| `serial_port_win.cpp` | termios USB serial, POSIX sockets | COM port (115200, never 1200 baud), Winsock |
| `discovery_win.cpp` | IOKit USB search, `rma.local` lookup | SetupAPI search for USB vendor 2E8A; `rma.local` through Windows, then a small mDNS query of its own |
| `pairing_win.cpp` | `~/Library/Application Support/RobotMeetingAlert/` (mode 600) | `%APPDATA%\RobotMeetingAlert\` (file readable only by your Windows user) |
| `http_win.cpp` | NSURLSession | WinHTTP (system proxy settings) |
| `outlook_calendar.cpp` | EventKit | classic Outlook's COM interface: reads only start, end, all-day, busy, cancelled and declined |

Settings live in `%APPDATA%\RobotMeetingAlert\project3.ini`; delete that
folder to start over.

### Known limits

- Untested on hardware (see the top).
- Classic Outlook only; the new Outlook needs the iCal link.
- Outlook's date filter uses your Windows date format. If meetings are
  missing with an unusual regional format, please report it.
- The window may look blurry on high-DPI screens.
