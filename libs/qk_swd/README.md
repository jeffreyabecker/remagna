# qk_swd

The WCH single wire debug protocol (the one CH32V003 class parts use instead of
SWD) modelled as C++ templates: the protocol is a CRTP base, the port line
derives from it, the debug module derives from the port, and nothing anywhere is
virtual.

This is a reading aid first: the interesting content is the timing contract and
the frame layout, not a port of the reference programmer.

## Files

| file | contents |
|------|----------|
| `qk_swd_wire.h` | wire format, timing table, `qkSwdTiming`, `qkSwdWire` (CRTP base) |
| `qk_swd.h` | register map, status/width types, `qkSwd` declaration |
| `qk_swd_impl.h` | the debug module semantics on top of the frames |

Everything is header only: `qk_swd.h` includes `qk_swd_impl.h` at the bottom.

```
qkSwdWire<Port>   bits, frames, timing, line reset             <- the protocol
Port              five pin calls, inherits the frames          <- hardware
qkSwd<Port>       registers, halt, abstract commands, PROGBUF  <- the meaning
```

Each layer derives from the one below, so the whole stack is a single object: the
pin, the frames and the debug module. The port is not stored inside the wire and
the wire is not stored inside the debugger; `struct MyLine : qkSwdWire<MyLine>`
inherits `write()`, `read()`, `writeContinue()`, `readContinue()`, `reset()`,
`sendBit()`, `readBits()` and friends, supplies the five pin calls the wire
reaches through a single `static_cast`, and `qkSwd<MyLine>` adds the debug module
on top of that.

Substituting the type rather than dispatching through a base interface is what
keeps the timed path clean: a frame is 41 timed phases and an indirect call
costs a visible slice of a 250 ns budget, spent inside the timed section, because
the wait only starts once the call has landed. As a CRTP base the line calls
compile to plain register writes and the generated code has no indirect branches
at all.

## Usage

```cpp
struct MyLine : qkSwdWire<MyLine>
{
  void driveLow() { ... }           // pin low, push pull
  void driveHigh() { ... }          // pin high, push pull
  void release() { ... }            // pin input float
  bool sample() { ... }             // pin level
  void waitNs(uint32_t ns) { ... }  // never returns early
};

qkSwd<MyLine> debugger;

if (debugger.probe() != qkSwdStatus::Ok) { /* no target */ }
debugger.halt();

debugger.write(qkSwdRegister::RegCtrl, 0x00000001u);  // typed register frame
debugger.write(0x11, 0x12345678u);                   // or a raw byte address
debugger.setTiming(qkSwdTiming::fast());
```

`qkSwdStatus`, `qkSwdWidth` and `qkSwdRegister` live at file scope so a call site
never has to spell out the template argument.

Everything the wire offers is on the same object: `write()`, `read()`,
`writeContinue()`, `readContinue()`, `reset()`, `setTiming()`, `pauseNs()`, and
the bit primitives `sendBit()`, `sendWordBits()`, `readBits()`, `sendStop()` that
the frames are built from.

## The system in one page

A frame is `start bit | 8 bit address byte | 32 bit data | stop bit`.

The address byte is the 7 bit register address shifted left once, with the
direction bit as the LSB: `0` means the target drives the data phase back,
`1` means the master writes it. A burst replaces the address phase with a
`0` shaped start bit, which means "same register, next word".

The debug module has no memory access command. Memory is reached by hand
assembled RISC-V: the address is staged in `DATA1`, eight words of code are
copied into `PROGBUF0..7`, `COMMAND` starts them on the halted hart, and the
value travels through `DATA0`. Bursts work because `ABSTRACTAUTO` bit 0 re-runs
the last command on every `DATA0` access while the program itself bumps the
pointer in `DATA1`.

So a "read a word of memory" is: 2 register writes, 8 register writes of code,
1 command write, a poll of `ABSTRACTCS`, 1 register read. Roughly 12 frames, and
every frame is 41 bits.

## Timing

All phases are minimums, in nanoseconds, `qkSwdTiming`:

| phase | medium | fast | why |
|-------|--------|------|-----|
| 1 bit low | 250 | 165 | short enough that the target reads a 1 |
| 1 bit high | 250 | 165 | line back up before the next bit |
| 0 bit low | 750 | 490 | this duration *is* the 0, most jitter sensitive |
| 0 bit high | 250 | 165 | |
| read low | 250 | 165 | master owns the line before sampling |
| read hold | 250 | 165 | wait out the target's 0 phase |
| stop | 2900 | 1700 | released high, once per frame |
| wake pulse | 20 ms | 20 ms | line reset, longer than any target timeout |

Two consequences worth remembering:

* A frame cannot be interrupted. There is no resynchronisation inside a frame,
  so an IRQ between the address and data phases corrupts it silently.
* There is no acknowledge. Writes are fire and forget, reads always return 32
  bits, and a dead target reads as all ones. That is why `probe()` compares the
  capability register against `0x00010403` instead of checking that the line
  moved.

## Porting

Derive from `qkSwdWire<YourLine>` and implement the five calls: a GPIO for the
four drive/sample calls and any monotonic time base (timer counter, cycle
counter, `SYSTICK`) for `waitNs()`. On ch32fun that is `funPinMode` /
`funDigitalWrite` / `funDigitalRead` plus a free running timer read, with the pin
switched between output push-pull and input float on every `driveLow()` /
`release()`.

The wire calls each of the five through `static_cast<Derived *>(this)`, so a
missing or misspelled one shows up as a compile error inside `qkSwdWire` naming
the call.

`waitNs()` only has to be accurate to a few tens of nanoseconds; overshooting is
always safe, undershooting a `0` phase turns it into a `1`.

## Reference

The protocol was reverse engineered from the WCH single line programmer shipped
in `libs/ch32v003` (`.../CH32V003_1Line_SDIO_Program_Base_on_CH32F103/
CH32V003_1Line/.../components/singleline.c`), which is the file to read next to
this one.
