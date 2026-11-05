# ZP008-p-v10 — magna peripheral firmware (PY32F002B)

Firmware for the peripheral side of the `magna` machine: the controller clocks a
register file over a two-wire bit-banged link, and this device answers with the
button, battery and pressure state while applying the display, pump and valve
registers it is given.

Target: **PY32F002BF15P6**, TSSOP20, Cortex-M0+, 24 KB flash / 3 KB RAM.

## Layout

| Path | Contents |
| --- | --- |
| `Src/main.cpp` | `HAL_Init`, clock, LED, the `peripheral` instance, the CLK interrupt handler (overrides the weak stub), non-blocking loop (`poll()` + heartbeat) |
| `Src/Py32Peripheral.h` / `Py32Peripheral.cpp` | `class Py32Peripheral final : public Peripheral` — CLK/DATA pins, CLK edge handling, register ↔ board mapping |
| `Src/board.h` | The only project header: HAL module selection and system values, pin map, board bring-up and interrupt prototypes |
| `Src/board.c` | The only C file: clock tree, `HAL_MspInit`, weak defaults for every interrupt vector |
| `Src/py32f002b_hal_conf.h` | Vendor include hook: includes `board.h` and the selected HAL module headers |

Everything vendor-owned comes from git at build time — nothing is copied into
this tree:

* `PY32F002B_Firmware/` (submodule) — CMSIS, the HAL sources, `SystemInit`, the
  `startup_py32f002bxx.s` startup file and the `py32f002bx5.ld` linker script
  (all from `Templates/PY32F002Bxx_Templates/` and `Drivers/`).
* `../libs/bitbang_interface` — the protocol engine behind
  [`protocol.cpp`](../libs/bitbang_interface/src/protocol.cpp), so the peripheral and
  the controller always share one definition of the frame format and the
  register map (see [protocol.h](../libs/bitbang_interface/src/protocol.h)).

The only duplicated-basename trap is `Templates/PY32F002Bxx_Templates/Src`,
which holds its own `main.c`, `py32f002b_it.c` and `py32f002b_hal_msp.c`. That
directory is deliberately kept off the make `vpath`; `SystemInit` is built
through its own `vendor_*.o` rule instead.

### Why py32f002b_hal_conf.h still exists

`py32f0xx_hal.h` includes `py32f002b_hal_conf.h` as its first include, before
any HAL or device type exists — so that filename has to be on the include path
and has to be able to include the module headers. Since `board.h` is already
mid-include when an application file starts the chain, its guard would make the
re-entry a no-op. The split is therefore:

* `board.h` — module selection, oscillator/system values, assert macro, pin map,
  prototypes (the configuration).
* `py32f002b_hal_conf.h` — includes `board.h`, then the selected module headers
  (the mechanism the vendor header demands).

## Build

```sh
# once, if the submodule is not checked out yet (run from the repo root)
git submodule update --init ZP008-p-v10/PY32F002B_Firmware

# arm-none-eabi-gcc must be on PATH, or point GCC_PATH at its bin directory
make
# or
make GCC_PATH="C:/Program Files/Raspberry Pi/Pico SDK v1.5.1/gcc-arm-none-eabi/bin"
```

Outputs in `build/`:

* `ZP008-p-v10.elf` — debug image
* `ZP008-p-v10.hex` — for the programmer
* `ZP008-p-v10.bin` — raw image
* `ZP008-p-v10.map` — link map

`make clean` removes `build/`.

All application code is C++; only [board.c](Src/board.c) stays C so the HAL MSP
and the interrupt vectors keep plain C linkage. The C++ side is built with
exceptions and RTTI disabled; the image uses ~5.2 KB of flash and ~1.1 KB of RAM
including the heap/stack reservation.

## Flashing

The part is SWD-only (PB6/SWD and PA2/SWCLK). Leave PC0 (SWDIO) and PA2
(SWCLK) unconnected to anything else on the board.

```sh
pyocd load build/ZP008-p-v10.hex --target=PY32F002Bx5 --frequency=1000000
```

or

```sh
openocd -f interface/cmsis-dap.cfg -f target/py32f002b.cfg \
        -c "program build/ZP008-p-v10.hex verify reset exit"
```

## Pins

| Signal | Pin | TSSOP20 pin | State |
| --- | --- | --- | --- |
| Link CLK | PA0 | 16 | assigned |
| Link DATA | PB1 | 14 | assigned |
| Heartbeat LED | PA1 | 17 | assigned (Start Kit LED3) |
| Display, buttons, battery, pressure ADC, pump, valve | — | — | **unassigned (TBD for ZP008 rev 10)** |

CLK and DATA are declared in [board.h](Src/board.h). The CLK pin number also
selects the EXTI vector, which [Py32Peripheral.cpp](Src/Py32Peripheral.cpp) and
[board.c](Src/board.c) derive automatically; keep both pins in the 0..7 range,
since the PY32F002B only implements `EXTICR[0..1]`.

Bringing up the rest of the board only needs the `TODO(ZP008 rev 10)` spots in
[Py32Peripheral.cpp](Src/Py32Peripheral.cpp) to be filled in: the class already
publishes and applies every register.

## Bus behaviour

* CLK idles low, DATA idles high (released, internal pull-up).
* The CLK EXTI interrupt runs the whole frame engine; the main loop never
  blocks, so the controller can clock as fast as the ISR latency allows.
* `board.c` only provides weak defaults for the vectors, so `main.cpp` owns the
  CLK interrupt (`BOARD_LINK_CLK_IRQHandler`, i.e. the EXTI vector selected by
  the CLK pin) together with the `Py32Peripheral` instance it serves.
* A frame left half-transferred (controller stopped clocking for 10 ms) is
  dropped by `Py32Peripheral::poll()`.
* `Py32Peripheral::poll()` reports which output registers were written (through
  `takeUpdates()`), so the board is only touched for values that actually
  changed.
* Multi-byte registers are read and written with the CLK interrupt masked, so a
  frame can never observe a half-updated 16- or 32-bit value.
