/**
  ******************************************************************************
  * @file    ZP008-C-v10.ino
  * @brief   Arduino sketch: PERIPHERAL device for the manga machine.
  *
  * Board: ZP008-C-v10.
  * Target: CH32V003F4P6 (TSSOP20, 16 KB flash / 2 KB SRAM) running the
  * CH32V003 Arduino core. Adjust the pin defines in ch32_constants.h to match
  * your board package.
  *
  * ch32Peripheral (see ch32Peripheral.h) implements the Peripheral engine from
  * the bitbang_interface library: setup() brings the board up, loop() keeps the
  * input registers fresh. The interrupt and the output registers are handled
  * here, next to the wiring they belong to.
  *
  * Wiring:
  *   CLK   <- controller CLK
  *   DATA  <-> controller DATA (open-drain / pulled up)
  *   GND common
  ******************************************************************************
  */

#include <Arduino.h>

#include <cstdint>

#include "ch32Peripheral.h"
#include "ch32_constants.h"

ch32Peripheral peripheral;

/** CLK interrupt: hand the edge to the protocol engine. */
void onClockEdge()
{
  peripheral.onClockEdge();
}

/** Apply whatever the controller wrote since the last call. */
void applyOutputs()
{
  const uint8_t updates = peripheral.takeUpdates();

  if ((updates & Peripheral::UpdateDisplay) != 0U)
  {
    char digits[DisplayDigits];
    peripheral.display(digits);
    (void)digits;
    /* No panel is wired up in this sketch yet: read the digits here. */
  }

  if ((updates & Peripheral::UpdatePumpDuty) != 0U)
  {
    /* PumpDutyMax (0..1000) maps onto the 0..255 PWM range. */
    const uint16_t pwm = static_cast<uint16_t>(
        (static_cast<uint32_t>(peripheral.pumpDuty()) * 255U + PumpDutyMax / 2U) / PumpDutyMax);
    analogWrite(PIN_PUMP, pwm);
  }

  if ((updates & Peripheral::UpdateValveState) != 0U)
  {
    digitalWrite(PIN_VALVE, peripheral.valveOpen() ? HIGH : LOW);
  }
}

void setup()
{
  peripheral.setup();

  attachInterrupt(digitalPinToInterrupt(PIN_CLK), onClockEdge, CHANGE);
}

void loop()
{
  peripheral.loop();
  applyOutputs();
}
