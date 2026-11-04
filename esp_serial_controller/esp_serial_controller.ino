/**
  ******************************************************************************
  * @file    esp_serial_controller.ino
  * @brief   Arduino sketch: CONTROLLER device for the manga machine, driven over
  *          the USB serial port.
  *
  * Board: ESP32-C3 Super Mini (esp32:esp32:nologo_esp32c3_super_mini).
  *
  * Roles:
  *   controller device (this sketch) - drives the link and owns its timing
  *   peripheral device               - ZP008-p-v10 (PY32F002B) or ZP008-C-v10
  *                                     (CH32V003); both implement Peripheral
  *
  * Wiring:
  *   GPIO4 (CLK)   -> peripheral CLK
  *   GPIO5 (DATA)  <-> peripheral DATA (open-drain / pulled up)
  *   GND common
  *
  * EspController is the platform side of the link, SerialController the command
  * line that exposes the register map (see SerialController.h for the syntax).
  *
  * Button 1 starts a pump cycle, button 2 stops it. The pump is also stopped
  * when the peripheral reports more than 30 s of pressurized time.
  ******************************************************************************
  */

#include <Arduino.h>

#include <protocol.h>

#include "EspController.h"
#include "SerialController.h"

namespace
{
constexpr uint8_t PinClk = 4;
constexpr uint8_t PinData = 5;

/* Half bit time. Both peripheral projects drive DATA from the CLK interrupt, so
 * anything from a few microseconds upwards works; 100 us keeps a scope and a
 * polled slave happy. */
constexpr unsigned HalfBitUs = 100U;
} // namespace

EspController panel(PinClk, PinData, HalfBitUs);
SerialController console(panel, Serial);

namespace
{
/** Set the pump duty and verify the peripheral applied it. */
bool setPumpDuty(uint16_t tenthsPercent)
{
  return panel.writePumpDuty(tenthsPercent) && (panel.pumpDuty() == tenthsPercent);
}

/** Stop the pump if the system has been pressurized for too long. */
void pressureGuard()
{
  if (panel.pressurizedTimeMs() > 30000U)
  {
    setPumpDuty(0);
    Serial.println(F("pressure guard: pump stopped"));
  }
}

/** Open the valve and ramp the pump to 60 %. */
void runPumpCycle()
{
  panel.writeValveState(ValveState::Open);

  for (uint16_t duty = 0; duty <= 600U; duty += 10U)
  {
    if (!setPumpDuty(duty))
    {
      break;
    }
    delay(50);
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);

  panel.begin();

  // Safe state: pump stopped, valve closed.
  setPumpDuty(0);
  panel.writeValveState(ValveState::Closed);

  console.begin();
}

void loop()
{
  console.poll();

  const uint8_t buttons = panel.buttons();

  if (pressed(buttons, 1U))
  {
    runPumpCycle();
  }
  else if (pressed(buttons, 2U))
  {
    setPumpDuty(0);
    panel.writeValveState(ValveState::Closed);
  }

  pressureGuard();

  delay(20);
}
