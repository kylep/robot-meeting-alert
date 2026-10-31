# Assembly guide: project 2 (RGB light, melodies, knob)

This turns the working **project 1** board into project 2. You'll take out
the three single LEDs, swap the beeper for the buzzer that can play notes, and
add an RGB LED, a knob and a diode.

It takes about 20 minutes. **Keep the USB cable unplugged** until section 6.

The board, hole names and rails work exactly like project 1: column numbers
run 1 → 63 left to right, and row **a** is nearest you. The **top** (far)
rails are the outer **red (+) = 5 V** and inner **blue (−) = ground**. See
[project 1's guide](../../project-1/docs/assembly.md#0-before-you-start) if you need a refresher.

Wire colours: **red** = 5 V, **orange** = 3.3 V (new), **yellow** = signal,
**black** = ground. Any jumper works; the colours just help you check.

## 1. Parts

| Qty | Part | How to recognise it |
|----:|------|---------------------|
| 1 | RGB LED | clear, **four legs**; one leg is longest |
| 1 | Passive buzzer | **no sticker**; you can see a little green circuit board underneath |
| 1 | Potentiometer (the knob) | round body, ridged shaft, three legs |
| 1 | Diode 1N4001 | small black cylinder with a **silver stripe (band)** at one end |
| 3 | 220 Ω resistors | the ones you take out in section 2 (red red black black brown) |
| 7 | Jumper wires | any colours |

## 2. Take out the project 1 lights

Pull each part straight up. Put them back in the kit box.

1. The red LED (e28, e29), the green LED (e35, e36) and the blue LED (e42, e43).
2. The three 220 Ω resistors: b24–b28, b31–b35 and b38–b42. Keep them, you'll reuse them.
3. Wires 4 to 9: the three yellow wires (a14→a24, b15→a31, a16→a38) and the
   three black wires (a29, a36, a43 → bottom blue rail).

**Leave everything else**: the Pico, Wires 1, 2, 3, 10, 11, 12, the 1 kΩ
resistor (i50–i55) and the transistor (j54–j56).

## 3. Swap the buzzer

1. Pull out the buzzer with the sticker (f56, f59).
2. Put in the **passive** buzzer (no sticker) in the same holes: **+ leg in f59**,
   other leg in f56.

## 4. Add the new parts (one at a time)

| Step | Part | Where | Notes |
|-----:|------|-------|-------|
| 1 | **RGB LED** | **g30 g31 g32 g33** | Longest leg (the common +) in **g31**. Left to right: red leg g30, **longest** g31, green g32, blue g33 |
| 2 | 220 Ω resistor | **f30 → e30** | stands across the centre gap |
| 3 | 220 Ω resistor | **f32 → e32** | |
| 4 | 220 Ω resistor | **f33 → e33** | bend 3 and 4 slightly apart so their legs don't touch |
| 5 | **Wire 13** (yellow) | **a14 → a30** | GP10 → red |
| 6 | **Wire 14** (yellow) | **b15 → b32** | GP11 → green |
| 7 | **Wire 15** (yellow) | **a16 → a33** | GP12 → blue |
| 8 | **Wire 16** (orange) | **i5 → i31** | 3.3 V → RGB common leg. **i5, not i4!** |
| 9 | **Knob** | **f40 f42 f44** | legs into row f, the knob's shaft pointing toward you (its body hangs over the centre gap, over Wire 10: that's fine) |
| 10 | **Wire 17** (orange) | **j31 → j44** | 3.3 V on to the knob's right leg |
| 11 | **Wire 18** (yellow) | **j10 → j42** | knob's middle leg → GP26. **j10, not j11!** |
| 12 | **Wire 19** (black) | **j40 → top blue rail** | knob's left leg → ground (so turning right = more) |
| 13 | **Diode** | **i56 → i59**, **band on i59** | protects the transistor from the buzzer's coil. Its body can sit a little raised |

If the knob's legs are only **one hole apart** (they fit f40 f41 f42), use
those: Wire 17 to **j42**, Wire 18 to **j41**, Wire 19 stays in **j40**.

## 5. Hole map and checks

Generated from the tables above, collision-checked, and cross-checked against
the [design netlist](design.md#3-hardware-changes-from-the-phase-1-board).

```
        1   5   10   15   20   25   30   35   40   45   50   55   60
far +   1+++++++++++++++++++++++++++++++++++++++++++++++++++++++++W+   red line (outer): 5 V
far -   ------------2--------------------------S-------------Y------   blue line (inner): ground
    j   1........Q..2.................P........S.Q.P.........EBC..W.
    i   ....N.........................N..................R....Rd..|.
    h   oooooooooooooooooooo.................................Y......
    g   ####################.........r+gb................X..........
    f   ####################.........=.==......[.w.]...........v..^.
        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   centre channel
    e   ####################.........=.==...........................
    d   ####################........................................
    c   oooooooooooooooooooo........................................
    b   ..............L....X...........L............................
    a   .............K.M.3...........K..M...........................
near +  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++   red line (inner): unused
near -  -----------------3------------------------------------------   blue line (outer): ground (Wire 3, now unused)
        1   5   10   15   20   25   30   35   40   45   50   55   60
```

Legend: `o` Pico pin, `#` under the Pico; wires `1` `2` `3` (Wires 1–3),
`X` Wire 10, `Y` Wire 11, `W` Wire 12, `K` `L` `M` Wires 13–15, `N` `P` `Q`
`S` Wires 16–19; `r` `+` `g` `b` RGB legs (red, common, green, blue); `=`
220 Ω legs; `R` 1 kΩ legs; `[` `w` `]` knob legs (end, middle, end);
`E` `B` `C` transistor; `v` `^` buzzer −/+; `d` `|` diode (plain end, band).

**What connects to what:**
- **5 V:** Pico VBUS – Wire 1 – top red rail – Wire 12 – buzzer + – diode band
- **3.3 V:** Pico 3V3 (h5) – Wire 16 – RGB common leg – Wire 17 – knob right leg (f44)
- **Ground:** Pico GND (h13) – Wire 2 – top blue rail – Wire 11 (transistor E) and Wire 19 (knob left leg, f40)
- **Red/green/blue:** GP10/GP11/GP12 – Wires 13/14/15 – 220 Ω – RGB red/green/blue legs
- **Knob reading:** knob middle leg – Wire 18 – GP26
- **Buzzer:** GP15 – Wire 10 – 1 kΩ – transistor B; transistor C – buzzer − – diode plain end

**Before plugging in, check:**
- [ ] The **top red rail** still holds only Wire 1 and Wire 12.
- [ ] The **orange** wires (3.3 V) go to i5, i31, j31 and j44 only, never to a blue rail.
- [ ] Nothing in **i4, j4** (3V3_EN) or **i11, j11** (RUN). Those pins switch off or reset the Pico.
- [ ] j2 and j3 are still empty.
- [ ] The RGB LED's **longest** leg is in **g31**.
- [ ] The **diode band is on i59** (the 5 V side). Backwards, it shorts the buzzer when it plays and gets hot.
- [ ] The buzzer is the one **without** a sticker, + leg in f59.
- [ ] No bare legs touch, especially the resistors at columns 32 and 33, and the
      1 kΩ leg (i55) and the diode leg (i56), which sit side by side.

## 6. Load the new firmware

The project 1 firmware can reboot itself for updates, so no BOOTSEL button
this time. Plug the USB cable in, quit any open Robot Meeting Alert app, then
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
| No colour lights at all | RGB longest leg not in g31; Wire 16 not in i5→i31 |
| One colour never lights | its 220 Ω resistor or its yellow wire is in the wrong column |
| Colours are wrong (asked for red, got a dim blue; green never lights) | RGB LED turned around: the longest leg must be in g31, second from the left |
| White looks pink | normal for this LED: tune with `rmactl bal` (section 7) |
| Buzzer silent | it's the sticker buzzer; + leg not in f59; diode backwards |
| **Buzzer hums or the diode/buzzer gets warm** | **unplug now.** Diode band must be on i59; check the buzzer legs |
| Knob reading stuck at 0 % or 100 % | Wire 18 not in the knob's middle leg column; Wire 17 or Wire 19 missing |
| Knob works backwards (right = less) | swap where Wire 17 and Wire 19 land on the knob (j44 ↔ j40, or j42 ↔ j40 if you used f40 f41 f42) |
| Pico keeps disconnecting or resetting | something in i11/j11 (RUN) or i4/j4 (3V3_EN): move it |
| App says "Looking for Pico…" | new firmware not loaded yet (`make flash`); charge-only cable |
| App says "Port busy" | quit the project 1 app or `rmactl`, then try again |
