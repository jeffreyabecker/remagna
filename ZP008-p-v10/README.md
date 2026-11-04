# ZP008-p-v10 — magna peripheral firmware (PY32F002B)

Firmware for the peripheral side of the `magna` machine: the controller clocks a
register file over a two-wire bit-banged link, and this device answers with the
button, battery and pressure state while applying the display, pump and valve
registers it is given.

Target: **PY32F002BF15P6**, TSSOP20, Cortex-M0+, 24 KB flash / 3 KB RAM.

## Layout

| Path | Contents |
| --- | --- |
| `Src/main.c` | `HAL_Init`, clock, board I/O, link, non-blocking main loop |
| `Src/link.cpp` | CLK/DATA pins, CLK edge ISR, DATA drive timing around the protocol engine |
| `Src/link.h` | C entry points used by the application (publish inputs, take updates) |
| `Src/app_peripheral.c` | Glue between the register map and the board: pressure timer, publish, apply |
| `Src/app_io.c` | Board I/O boundary (LED is real, the rest are stubs until pins are assigned) |
| `Src/board.h` | Package pin map and every pin the firmware uses |
| `Src/bsp/` | HAL MSP and interrupt vectors |
| `Src/py32f002b_hal_conf.h` | Which HAL modules are built in (project config, keep local) |
| `Src/py32f002b_it.h` | Interrupt handler prototypes (project header, keep local) |

Everything vendor-owned comes from git at build time — nothing is copied into
this tree:

* `PY32F002B_Firmware/` (submodule) — CMSIS, the HAL sources, `SystemInit`, the
  `startup_py32f002bxx.s` startup file and the `py32f002bx5.ld` linker script
  (all from `Templates/PY32F002Bxx_Templates/` and `Drivers/`).
* `../bitbang_interface` — the protocol engine behind
  [`protocol.cpp`](../bitbang_interface/src/protocol.cpp), so the peripheral and
  the controller always share one definition of the frame format and the
  register map (see [protocol.h](../bitbang_interface/src/protocol.h)).

The only duplicated-basename trap is `Templates/PY32F002Bxx_Templates/Src`,
which holds its own `main.c`, `py32f002b_it.c` and `py32f002b_hal_msp.c`. That
directory is deliberately kept off the make `vpath`; `SystemInit` is built
through its own `vendor_*.o` rule instead.

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

The C++ engine is built with exceptions and RTTI disabled; the image uses
~5.3 KB of flash and ~1.1 KB of RAM including the heap/stack reservation.

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
selects the EXTI vector, which `Src/bsp/py32f002b_it.c` derives automatically;
keep both pins in the 0..7 range, since the PY32F002B only implements
`EXTICR[0..1]`.

Bringing up the rest of the board only needs [app_io.c](Src/app_io.c) to be
filled in: the link layer already publishes and applies every register.

## Bus behaviour

* CLK idles low, DATA idles high (released, internal pull-up).
* The CLK EXTI interrupt runs the whole frame engine; the main loop never
  blocks, so the controller can clock as fast as the ISR latency allows.
* A frame left half-transferred (controller stopped clocking for 10 ms) is
  dropped by `Link_Task()`.
* `Link_TakeUpdates()` reports which output registers were written, so the board
  is only touched for values that actually changed.
* Multi-byte registers are read and written with the CLK interrupt masked, so a
  frame can never observe a half-updated 16- or 32-bit value.
