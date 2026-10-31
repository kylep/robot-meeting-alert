# Assembly guide: USB-controlled lights + buzzer

This guide builds the phase 1 circuit from the Freenove FNK0064E kit: a
Raspberry Pi Pico 2 W driving a red, a green and a blue LED, plus an active
buzzer. When you finish, the **Robot Meeting Alert** Mac app switches each
light on and off and sounds the buzzer while you hold its button.

It takes about 30 minutes. There's no soldering, and everything stays
unplugged until step 8.

The circuits come from the kit's own tutorial (LED: chapter 1, buzzer:
chapter 6). The layout and pins are specific to this project.

---

## 0. Before you start

**Cable.** The Pico 2 W has a **micro-USB** port. Use a data cable (not
charge-only) from your Mac to the Pico.

**Clear the breadboard.** The kit ships with some parts pressed into the
breadboard to protect their pins, including the 10-segment LED bar in the far
half near the right-hand end (around columns 46–58). Pull them out: lift one end slightly, then the other, then lift the
part off. A small screwdriver or the kit's crowbar helps.

**Turn the breadboard so it reads the right way.** Lay it flat with the long
side toward you, then check all three:

1. Column numbers run **1 → 63 from left to right**.
2. Row letters read **a, b, c, d, e** in the half **nearest you** and
   **f, g, h, i, j** in the far half. Row **a** is the row closest to you.
3. Each long edge has two power rails, marked by a **red line (+)** and a
   **blue line (−)**. On the near edge the **red** rail is the inner one, next
   to row a, and the **blue** rail is the outer one. On the far edge the
   **blue** rail is inner, next to row j, and the **red** rail is outer.
4. The board has **at least 60 numbered columns**. If it has fewer, stop: this
   layout won't fit.
5. Each rail's coloured line runs **unbroken from end to end**. If a line has
   a gap in the middle, that rail is split into two halves. Bridge each split
   rail you use with one jumper across the gap: the far red rail, the far blue
   rail and the near blue rail.

> If your board's letters run the other way (row **a** farthest from you),
> don't rotate anything. Swap every letter in this guide: a↔j, b↔i, c↔h,
> d↔g, e↔f, and swap "near rail" with "far rail". The physical holes come out
> the same.

**How the breadboard connects.** Each column's five holes in one half (for
example c14, d14, e14 and b14, a14) are joined underneath. The two halves are
separate, split by the centre channel. Each rail is joined along its whole
length.

## 1. Parts

From the kit:

| Qty | Part | How to recognise it |
|----:|------|---------------------|
| 1 | Raspberry Pi Pico 2 W | green board, "Pico 2 W" printed on it |
| 1 | USB cable | (or your own micro-USB data cable) |
| 1 | Breadboard | |
| 3 | Resistor 220 Ω | 5-band: **red red black black brown**. 4-band: red red brown gold |
| 1 | Resistor 1 kΩ | 5-band: **brown black black brown brown**. 4-band: brown black red gold |
| 1 | Red LED, 1 Green LED, 1 Blue LED | the **longer leg is +** (anode) |
| 1 | Active buzzer | **sticker on top** ("REMOVE SEAL AFTER WASHING") and a **sealed black bottom**. The passive buzzer has no sticker and you can see a circuit board underneath; don't use that one. |
| 1 | NPN transistor **S8050** | black, three legs, **"8050"** printed on the flat face. Don't use the 8550. |
| 12 | Jumper wires | any colours. Red for +5 V and black for ground makes checking easier |

Use the kit's resistor colour-code card if the bands are hard to read. Keep
the buzzer's sticker on for now; peel it later if you want it louder.

## 2. Safety

- **Keep the USB cable unplugged** until step 8.
- **VBUS is 5 V straight from your Mac.** Its wire (W1) and the buzzer's
  wire (W12) are the only things that go in the **far red (+) rail**. Nothing
  connected to VBUS may touch a blue rail or a GND pin.
- Nothing goes into the Pico's 3V3 pins in this build.

## 3. Place the Pico

Hold the Pico **components up** with the **USB connector pointing left**, off
the left end of the breadboard.

Press it across the centre channel so that:

- its **near row of pins** (pins 1–20) sits in **row c**, columns **1–20**
- its **far row of pins** (pins 21–40) sits in **row h**, columns **1–20**

That leaves two free holes per column on each side (a/b near, i/j far).
Freenove's tutorial drawings use rows b and g. **This build uses c and h** on
purpose.

**Check before going on.** The tiny labels printed next to the pins should
read:

| Hole | Pico label |
|------|-----------|
| c1 | GP0 / 0 |
| c18 | GND |
| c20 | GP15 / 15 |
| h1 | VBUS |
| h13 | GND |

If c1 isn't GP0, the Pico is rotated. Lift it out evenly and fix it now.

## 4. Power wires

| Wire | From | To |
|------|------|----|
| **W1** | j1 (VBUS) | **far red (+) rail**, the outer one |
| **W2** | j13 (GND) | **far blue (−) rail**, the inner one |
| **W3** | a18 (GND) | **near blue (−) rail**, the outer one |

**Check:** W1 and W2 must go into **different** far rails: W1 in red, W2 in
blue. If both are in the same rail, the USB 5 V is shorted to ground.

**Leave the holes next to W1 empty.** j2 is VSYS (pin 39), which is tied to
VBUS inside the Pico, so ground must never touch it. That's why W2 uses the
GND pin at h13 instead of the closer one at h3.

## 5. The three LEDs (near half)

Each LED gets a wire from its Pico pin, then a 220 Ω resistor, then the LED,
then a wire to ground. Put each LED's **long leg** in the first hole listed.

| LED | Pico pin | Wire to resistor | 220 Ω resistor | LED long (+) / short (−) | Ground wire |
|-----|----------|------------------|----------------|--------------------------|-------------|
| Red | GP10 (c14) | **W4** a14 → a24 | **R1** b24 → b28 | e28 / e29 | **W5** a29 → near blue (−) |
| Green | GP11 (c15) | **W6** b15 → a31 | **R2** b31 → b35 | e35 / e36 | **W7** a36 → near blue (−) |
| Blue | GP12 (c16) | **W8** a16 → a38 | **R3** b38 → b42 | e42 / e43 | **W9** a43 → near blue (−) |

Resistors work either way round. LEDs only work one way: long leg toward the
resistor.

## 6. The buzzer (far half)

The Pico can't power the buzzer directly. The S8050 transistor acts as a
switch, letting GP15 turn the 5 V supply to the buzzer on and off.

1. **W10**: b20 (GP15) → g50. This wire crosses the centre channel.
2. **R4** (1 kΩ): i50 → i55.
3. **Q1** (S8050): put its legs in **j54, j55, j56** with the **flat printed
   face toward you** (toward the centre channel). Reading the flat face, the
   legs from left to right are **E, B, C**:
   E (emitter) = j54, B (base) = j55, C (collector) = j56.
4. **W11**: h54 → **far blue (−) rail**.
5. **Buzzer**: **− leg in f56**, **+ leg in f59**. The **+** is printed on
   top and is the longer leg. The buzzer body overhangs the centre channel;
   that's fine because those holes are unused.
6. **W12**: j59 → **far red (+) rail**.

## 7. Hole map and checklist

The map below is generated from the tables above and was cross-checked
against the circuit summary below and `firmware/src/pins.hpp`. Rows j…a run
top to bottom; the near side (row a) is at the bottom, nearest you.

```
        1   5   10   15   20   25   30   35   40   45   50   55   60
far +   1+++++++++++++++++++++++++++++++++++++++++++++++++++++++++W+   red line (outer)
far -   ------------2----------------------------------------Y------   blue line (inner)
    j   1...........2........................................EBC..W.
    i   .................................................R....R.....
    h   oooooooooooooooooooo.................................Y......
    g   ####################.............................X..........
    f   ####################...................................v..^.
        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   centre channel
    e   ####################.......Ll.....Ll.....Ll.................
    d   ####################........................................
    c   oooooooooooooooooooo........................................
    b   ..............6....X...R...R..R...R..R...R..................
    a   .............4.8.3.....4....5.6....7.8....9.................
near +  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++   red line (inner) - unused
near -  -----------------3----------5------7------9-----------------   blue line (outer)
        1   5   10   15   20   25   30   35   40   45   50   55   60
```

Legend: `o` Pico pin, `#` under the Pico, `1`–`9` wires W1–W9 (both ends),
`X` W10, `Y` W11, `W` W12, `R` resistor leg, `L`/`l` LED long/short leg,
`E B C` transistor legs, `^`/`v` buzzer +/− legs.

**Circuit summary (what must be connected to what):**

- **5 V:** Pico VBUS (pin 40) – far red rail – buzzer +
- **Ground:** Pico GND pin 28 – far blue rail; Pico GND pin 18 – near blue
  rail; transistor E; all three LED short legs
- **Red:** GP10 (pin 14) – 220 Ω – red LED long leg
- **Green:** GP11 (pin 15) – 220 Ω – green LED long leg
- **Blue:** GP12 (pin 16) – 220 Ω – blue LED long leg
- **Buzzer:** GP15 (pin 20) – 1 kΩ – transistor B; transistor C – buzzer −

**Before plugging in, check:**

- [ ] Every wire, resistor and leg is in the hole the tables say.
- [ ] The far red (+) rail holds **only** W1 and W12.
- [ ] W1 (red rail) and W2 (blue rail) are in different rails.
- [ ] j2 and j3 are empty (VSYS and GND sit right next to VBUS).
- [ ] LED long legs are at e28, e35 and e42.
- [ ] The transistor's flat face points toward you and is marked 8050.
- [ ] No bare leads touch each other, especially around the buzzer and the transistor.

## 8. Load the firmware (first time only)

1. Hold down the **BOOTSEL** button on the Pico (the white button near the
   USB port).
2. While holding it, plug the USB cable into the Pico and your Mac.
3. Release BOOTSEL. A drive called **RP2350** appears in Finder.
4. In the project folder, run `make flash` (or drag
   `build/firmware/rma_firmware.uf2` onto the RP2350 drive).

The drive disappears and the Pico restarts with the new firmware. **Nothing
should light up or buzz.** If something does, unplug and recheck section 7.

Later firmware updates don't need BOOTSEL: run `make flash` while the app is
closed.

## 9. Smoke test from the terminal

Quit the app first, because only one program can use the port at a time.
Then run:

```sh
build/mac/rmactl hello         # → RMA 1 0.1.0
build/mac/rmactl led r on      # red lights and stays on
build/mac/rmactl led g on      # green
build/mac/rmactl led b on      # blue
build/mac/rmactl buzz 500      # half-second beep
build/mac/rmactl alloff        # everything off
```

Green and blue are noticeably dimmer than red. At 3.3 V through 220 Ω they
only get about 1–2 mA. That's expected and not a fault.

## 10. Use the app

```sh
make run
```

The status line turns green and shows the port. Flip each switch to turn its
light on and off. **Hold "Hold to buzz"** to sound the buzzer; it stops when
you let go.

Safety behaviour to try:

- Unplug the cable while buzzing → silence.
- Quit the app while buzzing → silence.
- Replug the cable → the app reconnects within a couple of seconds and turns
  your lights back on.

## 11. Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| An LED never lights | LED in backwards (long leg must be in row e next to the resistor); wire in the wrong column; its ground wire (W5/W7/W9) or W3 missing or in the red rail; rail split and not bridged |
| An LED is always on | its wire goes to a power pin instead of GP10/11/12 |
| Buzzer silent | passive buzzer used (no sticker); 8550 instead of 8050; transistor turned around; W1, W11 or W12 missing; buzzer + and − swapped |
| Buzzer always on | transistor's E and C swapped; collector column touching ground |
| Buzzer sounds while the RP2350 drive is showing | the board was reset into BOOTSEL mid-beep and the pin latched (a known RP2350 quirk). Unplug and replug. |
| App says "Looking for Pico…" | firmware not loaded yet (step 8); charge-only cable; Pico still in BOOTSEL mode |
| App says "Port busy" / `rmactl` exits 3 | another program has the port. Quit it, or find it with `lsof /dev/cu.usbmodem*` |
| Nothing works and the Pico gets warm | **unplug now**: VBUS is shorted to ground. Check W1, W2 and that j2 is empty (section 4). |
