/**
  ******************************************************************************
  * @file    esp_controller.ino
  * @brief   Arduino sketch: CONTROLLER device for the manga machine.
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
  * EspController is the platform side of the link, BleController the BLE front
  * end that mirrors the register map (see BleController.h for the GATT layout).
  *
  * Button 1 starts a pump cycle, button 2 stops it. The pump is also stopped
  * when the peripheral reports more than 30 s of pressurized time.
  ******************************************************************************
  */

#include <Arduino.h>

#include <protocol.h>

#include <EspController.h>

#include "BleController.h"

namespace
{
constexpr uint8_t PinClk = 4;
constexpr uint8_t PinData = 5;

/* Half bit time. Both peripheral projects drive DATA from the CLK interrupt, so
 * anything from a few microseconds upwards works; 100 us keeps a scope and a
 * polled slave happy. */
constexpr unsigned HalfBitUs = 100U;

/* How often the read-only characteristics are refreshed from the link. */
constexpr uint32_t RefreshPeriodMs = 200U;

uint32_t lastRefreshMs = 0U;
} // namespace

EspController panel(PinClk, PinData, HalfBitUs);
BleController ble;

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
    ble.publishPumpDuty(0);
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

/** Push the output registers back to BLE after the link applied a write. */
void publishOutputs()
{
  ble.publishPumpDuty(panel.pumpDuty());

  const std::optional<ValveState> valve = panel.valveState();
  ble.publishValve(valve.has_value() && (valve.value() == ValveState::Open));

  char digits[DisplayDigits] = {};
  panel.display(digits);
  ble.publishDisplay(digits);
}

/** Apply the writes the BLE client queued since the last pass. */
void applyBleWrites()
{
  const BleWrites writes = ble.takeWrites();

  if (writes.pumpDuty)
  {
    setPumpDuty(writes.pumpDutyValue);
    ble.publishPumpDuty(panel.pumpDuty());
  }

  if (writes.valve)
  {
    panel.writeValveState(writes.valveOpen ? ValveState::Open : ValveState::Closed);
    ble.publishValve(writes.valveOpen);
  }

  if (writes.display)
  {
    panel.writeDisplay(writes.displayDigits);
    ble.publishDisplay(writes.displayDigits);
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

  ble.begin();
  ble.publishDeviceInfo(panel.deviceId(), panel.mapVersion());
  publishOutputs();

  Serial.printf("magna controller: device 0x%02X, map %u\n", panel.deviceId(), panel.mapVersion());
  lastRefreshMs = millis();
}

void loop()
{
  applyBleWrites();

  const uint8_t buttons = panel.buttons();

  if (pressed(buttons, 1U))
  {
    runPumpCycle();
    publishOutputs();
  }
  else if (pressed(buttons, 2U))
  {
    setPumpDuty(0);
    panel.writeValveState(ValveState::Closed);
    publishOutputs();
  }

  pressureGuard();

  const uint32_t now = millis();
  if ((now - lastRefreshMs) >= RefreshPeriodMs)
  {
    lastRefreshMs = now;
    ble.publishButtons(buttons);
    ble.publishBattery(panel.batteryConnected(), panel.batteryMillivolts());
    ble.publishPressure(panel.pressureAdc(), panel.pressurizedTimeMs());
  }

  delay(50);
}
