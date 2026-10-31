# Assembly guide: project 2 (RGB light, melodies, knob)

The whole board, built from scratch around the Pico: an RGB LED, a buzzer
that plays notes, and a knob. This guide matches the
[assembly video](../README.md) hole for hole.

It takes about 20 minutes. **Keep the USB cable unplugged** until section 6.

The board and hole names work like project 1: column numbers run 1 → 63 left
to right, and row **a** is nearest you. The **top** (far) rails are the
outer **red (+) = 5 V** and inner **blue (−) = ground**. See
[project 1's guide](../../project-1/docs/assembly.md#0-before-you-start) for a refresher.

Wire colours: **red** = 5 V, **orange** = 3.3 V, **yellow** = signal,
**black** = ground. Any jumper works; the colours just help you check.

## 1. Parts

| Qty | Part | How to recognise it |
|----:|------|---------------------|
| 1 | Pico 2 W on the breadboard | USB end on the left, pins in rows c and h, columns 1–20. Take everything else off |
| 1 | RGB LED | clear, **four legs**; one leg is longest |
| 3 | 220 Ω resistors | red red black black brown |
| 1 | Potentiometer (the knob) | square body, round knob, three legs |
| 1 | Passive buzzer | **no sticker**; green circuit board visible underneath |
| 1 | NPN transistor S8050 | black, one flat face, three legs |
| 1 | 1 kΩ resistor | brown black black brown brown |
| 1 | Diode 1N4001 | small black cylinder with a **silver band** at one end |
| 12 | Jumper wires | long ones are fine: let them arc |

## 2. Power

| Step | Part | Holes |
|-----:|------|-------|
| 1 | **Wire 1** (red) | **j1 → top red rail** (VBUS: 5 V) |
| 2 | **Wire 2** (black) | **j13 → top blue rail** (ground) |

## 3. The knob

| Step | Part | Holes | Notes |
|-----:|------|-------|-------|
| 3 | **Knob** | **f26 f28 f30** | knob hangs toward you, over the centre gap |
| 4 | **Wire 3** (orange) | **i5 → i30** | 3.3 V to the knob's right leg. **i5, not i4!** |
| 5 | **Wire 4** (yellow) | **j10 → j28** | knob's middle leg to GP26. **j10, not j11!** |
| 6 | **Wire 5** (black) | **j26 → top blue rail** | knob's left leg to ground (turning right = more) |

If the knob's legs are only **one hole apart**, use f26 f27 f28: Wire 4 to
**j27**, Wire 3 to **i28**, and Wire 6 (next section) from **j28**.

## 4. The RGB light

| Step | Part | Holes | Notes |
|-----:|------|-------|-------|
| 7 | **RGB LED** | **g47 g48 g49 g50** | **longest leg in g49**; the short leg on its own (red) in g50; the other two (green, blue) in g48 and g47 |
| 8 | 220 Ω resistor | **f47 → e47** | stands across the centre gap (blue) |
| 9 | 220 Ω resistor | **f48 → e48** | (green) keep it from touching the first one |
| 10 | 220 Ω resistor | **f50 → e50** | (red) |
| 11 | **Wire 6** (orange) | **j30 → j49** | 3.3 V on to the LED's longest leg |
| 12 | **Wire 7** (yellow) | **a14 → a50** | GP10 → red |
| 13 | **Wire 8** (yellow) | **a15 → a48** | GP11 → green |
| 14 | **Wire 9** (yellow) | **a16 → a47** | GP12 → blue |

## 5. The buzzer

| Step | Part | Holes | Notes |
|-----:|------|-------|-------|
| 15 | 1 kΩ resistor | **f56 → e56** | across the centre gap |
| 16 | **Transistor** | **j55 j56 j57** | **flat side toward you** (E, B, C left to right) |
| 17 | **Wire 10** (black) | **h55 → top blue rail** | emitter to ground |
| 18 | **Buzzer** (no sticker) | **− f57, + f60** | |
| 19 | **Wire 11** (red) | **j60 → top red rail** | 5 V to the buzzer's + leg |
| 20 | **Diode** | **i57 → i60, band on i60** | protects the transistor from the buzzer's coil |
| 21 | **Wire 12** (yellow) | **b20 → b56** | GP15 to the 1 kΩ resistor |

## 5a. Hole map and checks

Generated from the video script and checked by a netlist script (every
connection traced from the Pico pins; nothing shorted; 3V3_EN, RUN and VSYS
untouched; diode band on 5 V).

```
        1   5   10   15   20   25   30   35   40   45   50   55   60
far +   1++++++++++++++++++++++++++++++++++++++++++++++++++++++++++B+++   red line (outer): 5 V
far -   ------------2------------5----------------------------A--------   blue line (inner): ground
    j   1........4..2............5.4.6..................6.....EBC..B...
    i   ....3........................3..........................d..|...
    h   oooooooooooooooooooo..................................A........
    g   ####################..........................bg+r.............
    f   ####################.....[.w.]................==.=.....Rv..^...
        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   centre channel
    e   ####################..........................==.=.....R.......
    d   ####################...........................................
    c   oooooooooooooooooooo...........................................
    b   ...................C...................................C.......
    a   .............789..............................98.7.............
near +  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++   unused
near -  ---------------------------------------------------------------   unused
        1   5   10   15   20   25   30   35   40   45   50   55   60
```

Legend: `o` Pico pin, `#` under the Pico; `1`–`9`, `A`, `B`, `C` Wires 1–12
(both ends); `[` `w` `]` knob legs (left, middle, right); `b` `g` `+` `r` RGB
legs; `=` 220 Ω legs; `R` 1 kΩ legs; `E` `B` `C` transistor; `v` `^` buzzer
−/+; `d` `|` diode (plain end, band).

**What connects to what:**
- **5 V:** Pico VBUS – Wire 1 – top red rail – Wire 11 – buzzer + – diode band
- **3.3 V:** Pico 3V3 (h5) – Wire 3 – knob right leg (f30) – Wire 6 – RGB longest leg (g49)
- **Ground:** Pico GND (h13) – Wire 2 – top blue rail – Wire 5 (knob left leg) and Wire 10 (transistor E)
- **Red/green/blue:** GP10/GP11/GP12 – Wires 7/8/9 – 220 Ω – RGB red/green/blue legs
- **Knob reading:** knob middle leg – Wire 4 – GP26
- **Buzzer:** GP15 – Wire 12 – 1 kΩ – transistor B; transistor C – buzzer − – diode plain end

**Before plugging in, check:**
- [ ] The **diode band is on i60** (the 5 V side). Backwards, it gets hot and the buzzer stays silent.
- [ ] Nothing in **i4, j4** (3V3_EN) or **i11, j11** (RUN). Those pins switch off or reset the Pico.
- [ ] The **orange** wires (3.3 V) go to i5, i30, j30 and j49 only, never to a rail.
- [ ] The RGB LED's **longest** leg is in **g49**.
- [ ] The transistor's flat side faces you; the buzzer is the one **without** a sticker, + leg in f60.
- [ ] No bare legs touch, especially the resistors at columns 47 and 48, and the
      1 kΩ leg in f56 next to the buzzer's − leg in f57.

## 6. Load the new firmware

If the Pico still has the project 1 firmware, it can reboot itself for
updates, so no BOOTSEL button. Plug the USB cable in, quit any open Robot Meeting Alert app, then
in the `project-2` folder run:

```sh
make mac flash   # builds the app and rmactl, then loads the firmware
```

## 7. Test from the terminal

```sh
build/mac/rmactl light on        # warm white-ish (all colours on)
build/mac/rmactl rgb 255 0 0     # red
build/mac/rmactl rgb 0 255 0     # green
build/mac/rmactl rgb 0 0 255     # blue
build/mac/rmactl rgb 255 255 255 # white: should look white, not pink
build/mac/rmactl play 1          # Westminster chime (also try 2, 3 and 4)
build/mac/rmactl meter both      # knob now controls brightness and volume
build/mac/rmactl watch 10        # turn the knob: the reading goes 0% to 100%
```

If white looks pink or blue, tune the white balance live, for example
`build/mac/rmactl bal 60 255 230`, and note the numbers that look right.

## 8. Use the app

```sh
make run
```

Flip **Light on**, pick a colour on the wheel or a swatch, choose a sound
(it plays a preview) or press **Play**, and pick what the knob controls. The
bar at the bottom shows where the knob is.

## 9. Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| No colour lights at all | RGB longest leg not in g49; Wire 3 not in i5→i30 or Wire 6 not in j30→j49 |
| One colour never lights | its 220 Ω resistor or its yellow wire is in the wrong column |
| Colours are wrong (asked for red, got blue) | RGB LED turned around: the longest leg must be in g49, with the single short leg in g50 |
| White looks pink | normal for this LED: tune with `rmactl bal` (section 7) |
| Buzzer silent | it's the sticker buzzer; + leg not in f60; diode backwards; transistor turned around |
| **Buzzer hums or the diode/buzzer gets warm** | **unplug now.** Diode band must be on i60; check the buzzer legs |
| Knob reading stuck at 0 % or 100 % | Wire 4 not in the knob's middle leg column; Wire 3 or Wire 5 missing |
| Knob works backwards (right = less) | swap the knob's outer-leg connections: Wire 3 to column 26, Wire 5 to column 30 (move Wire 6 to start from column 26 too) |
| Pico keeps disconnecting or resetting | something in i11/j11 (RUN) or i4/j4 (3V3_EN): move it |
| App says "Looking for Pico…" | new firmware not loaded yet (`make flash`); charge-only cable |
| App says "Port busy" | quit the project 1 app or `rmactl`, then try again |
