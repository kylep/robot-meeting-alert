# Project 3: the finished meeting alert

The same board as [project 2](../project-2/), with **no wiring changes**. It
now runs on its own from a USB phone charger and watches your calendar over
Wi-Fi:

| Robot light | Means |
|---|---|
| green | free |
| amber, gently breathing | a meeting starts within 5 minutes |
| red + your chosen melody | a meeting is starting |
| dim red | in a meeting |
| dim blue | it hasn't heard from the Mac for 15 minutes (asleep, off, away) |

How it works: the Mac app reads the Mac's own Calendar (any account added in
System Settings → Internet Accounts) and every 30 seconds tells the robot
"meetings start in N seconds". **Only times are sent, never titles or
people.** The robot counts down by itself, so a Wi-Fi hiccup never makes an
alert late. Your Wi-Fi password goes to the robot once over USB and is saved
on the robot, not in the app or this repo.

- **Firmware:** C++ on the Pico SDK 2.3.1 (`firmware/`): project 2's light,
  melodies and knob, plus Wi-Fi (CYW43 + lwIP), a TCP command port (4242),
  and mDNS (`rma.local`).
- **Mac app:** Dear ImGui + SDL3 + EventKit (`mac/`): calendar picker, Test
  alert, manual mode (project 2's controls), Wi-Fi setup.
- **Protocol v3:** project 2's plain-text lines, plus schedule and Wi-Fi
  commands, over USB or Wi-Fi (`shared/rma3/`).

Docs: [Design](docs/design.md) (architecture, protocol, review log) ·
[Work calendar options](docs/work-calendar.md) ·
[Kit inventory](../docs/inventory.md)

## Quick start

From this `project-3/` folder (toolchain setup:
[project 1](../project-1/README.md#toolchain-macos-apple-silicon)):

```sh
make test       # schedule/alert, protocol, Wi-Fi and link tests (no hardware)
make mac        # "build/mac/Robot Meeting Alert.app" and build/mac/rmactl
make firmware   # build/firmware/rma3_firmware.uf2
make flash      # silences the buzzer, then loads the firmware (USB)
make run        # opens the app
```

## First-time setup

1. Plug the robot into the Mac with USB and run `make flash`.
2. Give it your Wi-Fi: `build/mac/rmactl wifi` (it asks for the name and
   password; the password isn't shown or saved anywhere but the robot), or
   use **Wi-Fi setup** in the app. Check with `build/mac/rmactl net`: `W2`
   and an address means it's on Wi-Fi. The Pico's own little LED blinks while
   joining and stays on once connected.
3. **Give it a passphrase:** with the robot still on USB, open the app
   (`make run`), go to Settings → Robot passphrase, type one (8+ characters;
   a few random words is good) and click **Save, and set on robot**. Or run
   `build/mac/rmactl pair`. Over Wi-Fi the robot only obeys a Mac that knows
   the passphrase (see Security below).
4. Unplug it from the Mac and power it from any USB phone charger.
5. `make run`. The first time, macOS asks to let the app use your
   **calendars** and your **local network**: allow both. Choose **Calendar**
   mode and tick the calendars to watch.
6. Press **Test alert**: amber now, the melody in 10 seconds, red for 30
   seconds, then back to green.

Run the app from Finder or `make run`, not from inside tmux: macOS blocks
local-network access for programs started there.

**After rebuilding the app, check its permissions.** macOS remembers the
calendar and local-network permissions by the app's signature. Ours is a free
"ad hoc" signature that changes with every `make mac`, so after a rebuild
macOS may ask again, or quietly block the app. The app then says "Looking for
the robot" or shows no calendars even though nothing else is wrong. Check
System Settings → Privacy & Security → **Local Network** and **Calendars**
and switch Robot Meeting Alert back on before assuming the robot or the code
is broken.

### Switching to the work calendar (on the work laptop)

If the work account can't be added to the Mac's Calendar app, see
[docs/work-calendar.md](docs/work-calendar.md) for the other options
(stubbed in the app under Settings → Calendar source, not built yet).


1. Clone this repo and `make mac` on the work laptop.
2. Add the work account in System Settings → Internet Accounts (if your
   employer allows it), so it shows up in the Calendar app.
3. Open the app, go to Settings → Robot passphrase and type the same
   passphrase (**Save on this Mac**). No USB needed. Both Macs can now
   control the robot.
4. Allow calendars and local network, tick the work calendar, and quit the
   app on the personal Mac (only one should drive it at a time).

The robot must be on a Wi-Fi network the work laptop can reach. If it isn't
found automatically, type the robot's address (from `rmactl net`) under
**Wi-Fi setup → Robot address**.

## Sounds

Pick one as the alert sound (or preview it) in the app, or `rmactl sound N` / `rmactl play N`:

| # | Sound | # | Sound |
|---|---|---|---|
| 0 | Off | 6 | Frère Jacques |
| 1 | Westminster chime | 7 | Mary had a little lamb |
| 2 | Twinkle twinkle | 8 | Happy birthday |
| 3 | Ode to joy | 9 | Doorbell (ding-dong) |
| 4 | Beep | 10 | Chirp (quick rising notes) |
| 5 | Für Elise | | |

All public domain. Adding one = a few lines in `shared/rma3/melodies.cpp`, then `make flash`.

## rmactl

```
rmactl [--port PATH | --host IP] hello | state | light on|off | rgb R G B
       | sound N | play [N] | stop | meter none|bright|vol|both | bal R G B
       | watch SECONDS | mode cal|manual | net | wifi | wificlr
       | event IN_S DUR_S
```

`pair` asks for the robot passphrase and saves it on this Mac (and on the
robot, if it's on USB); `--host` then works over Wi-Fi.
`event 60 120` gives the robot one pretend meeting starting in 60 seconds and
lasting 2 minutes (calendar mode), for testing without a calendar. `wifi` and
`wificlr` work over USB only. Exit codes: 0 ok, 1 device error, 2 no robot,
3 port busy, 64 usage.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Pico LED keeps blinking | Wrong Wi-Fi name/password, or a 5 GHz-only network (the Pico needs 2.4 GHz). Plug in USB, `rmactl wifi` again |
| App says the robot "didn't accept this Mac's passphrase" | Retype it in Settings → Robot passphrase. Forgot it? Plug the robot in with USB and set a new one |
| App says "Looking for the robot" | Rebuilt the app recently? See "After rebuilding" above. Same Wi-Fi as the Mac? Allowed **Local Network** for the app (System Settings → Privacy & Security → Local Network)? Try the robot's address under Wi-Fi setup |
| Robot is dim blue | The app isn't running, or the Mac is asleep or on another network |
| No melody at a meeting | Alert sound set to Off, or the knob is at 0 in Volume/Both mode |
| Melody for a meeting I declined | Decline it in the calendar; the Mac skips declined, all-day and "free" events |
| Buzzer hums or gets warm | Unplug the robot (same safety rule as project 2) |

Security notes:
- **Passphrase.** Over Wi-Fi the robot only obeys a Mac that knows its
  passphrase. Each Mac turns the passphrase into a key (PBKDF2, deliberately
  slow) and keeps only the key, in
  `~/Library/Application Support/RobotMeetingAlert/pairing.key` (readable
  only by you). On connecting, the robot proves it has the key first (so a
  fake robot learns nothing), then the Mac does, and every command after
  that carries a signature tied to that connection: others on your Wi-Fi
  can't send, change or replay commands, or kick off an app that's in use.
  The passphrase itself never crosses the network. Setting or changing it
  on the robot needs USB.
- Pick a passphrase that isn't guessable (a few random words): someone who
  records the Wi-Fi traffic could try guesses offline.
- Commands over Wi-Fi are signed but not encrypted, so someone on your
  network could read them (meeting times, colours).
- Wi-Fi settings and pairing change only over USB. The Wi-Fi password and
  key are stored in plain text on the robot's flash (no secure storage on
  this board): run `rmactl wificlr` before giving the board away.

## Layout

```
shared/rma3/  device logic: protocol, schedule + alert stages, Wi-Fi record and
              supervisor, melodies, knob; host helpers incl. the meeting picker
firmware/     Pico SDK project (PWM, ADC, CYW43 Wi-Fi, lwIP TCP + mDNS, flash)
mac/          app (EventKit calendar, ImGui), rmactl, USB + TCP link, finder
tests/        host tests (CTest): device logic, plus fake robots on a pty and TCP
docs/         design
```
