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
  * Everything except the two GPIOs and the bit timing comes from the
  * bitbang_interface library: the frame format, the register map and the
  * transaction helpers behind Controller.
  *
  * Button 1 starts a pump cycle, button 2 stops it. The pump is also stopped
  * when the peripheral reports more than 30 s of pressurized time.
  ******************************************************************************
  */

#include <Arduino.h>
#include <protocol.h>

#if !defined(ARDUINO_ARCH_ESP32)
#error "esp_controller targets the ESP32-C3 Super Mini; select an ESP32-C3 board."
#endif

namespace
{
constexpr uint8_t PinClk = 4;
constexpr uint8_t PinData = 5;

/* Half bit time. Both peripheral projects drive DATA from the CLK interrupt, so
 * anything from a few microseconds upwards works; 100 us keeps a scope and a
 * polled slave happy. */
constexpr unsigned HalfBitUs = 100U;
} // namespace

/** Platform side of the contract: clock exactly one bit. */
class EspController final : public Controller
{
public:
  void begin()
  {
    pinMode(PinClk, OUTPUT);
    digitalWrite(PinClk, LOW);
    pinMode(PinData, INPUT_PULLUP);
  }

protected:
  bool clockBit(bool drive, bool txLevel) override
  {
    if (drive)
    {
      pinMode(PinData, OUTPUT);
      digitalWrite(PinData, txLevel ? HIGH : LOW);
    }
    else
    {
      pinMode(PinData, INPUT_PULLUP); // release for turnaround / read data
    }

    delayMicroseconds(HalfBitUs);
    digitalWrite(PinClk, HIGH);
    delayMicroseconds(HalfBitUs);
    const bool rx = digitalRead(PinData) == HIGH; // sample on the high phase
    digitalWrite(PinClk, LOW);
    return rx;
  }

  /** Let the bus idle high between transactions. */
  void releaseData() override
  {
    pinMode(PinData, INPUT_PULLUP);
  }
};

EspController panel;

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

void setup()
{
  Serial.begin(115200);
  panel.begin();

  // Safe state: pump stopped, valve closed.
  setPumpDuty(0);
  panel.writeValveState(ValveState::Closed);

  Serial.printf("magna controller: device 0x%02X, map %u\n", panel.deviceId(), panel.mapVersion());
}

void loop()
{
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
  delay(50);
}
