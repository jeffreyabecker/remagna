/**
  ******************************************************************************
  * @file    magna_controller.ino
  * @brief   Arduino sketch: CONTROLLER device for the manga peripheral.
  *
  * This sketch is an example of the bitbang_interface Arduino library. Install
  * or symlink the library folder into ~/Arduino/libraries/bitbang_interface
  * before opening it in the Arduino IDE.
  *
  * Roles:
  *   controller device (this sketch) - runs bitbang::Controller and sends commands
  *   peripheral device               - has the pump, sensor, buttons, valve and
  *                                     screen; implements the slave side
  *
  * Wiring:
  *   controller D18 (CLK)  -> peripheral CLK
  *   controller D19 (DATA) <-> peripheral DATA (open-drain / pulled up)
  *   GND common
  *
  * Button 1 starts a pump cycle, button 2 stops it. The pump is also stopped
  * when the peripheral reports more than 30 s of pressurized time.
  ******************************************************************************
  */

#include <controller.h>

using remagna::bitbang::Controller;
using remagna::bitbang::Status;
using remagna::bitbang::ValveState;
using remagna::bitbang::reg::kButton1;
using remagna::bitbang::reg::kButton2;

/**
  * @brief  Platform side of the contract: clock exactly one bit.
  */
class GpioController final : public Controller
{
public:
  GpioController(unsigned clkPin, unsigned dataPin, unsigned halfBitUs)
      : clk_(clkPin), data_(dataPin), half_(halfBitUs)
  {
  }

  /** Configure the pins; call from setup(). */
  void begin()
  {
    pinMode(clk_, OUTPUT);
    digitalWrite(clk_, LOW);
    pinMode(data_, INPUT_PULLUP);
  }

protected:
  bool clockBit(bool drive, bool txLevel) override
  {
    if (drive)
    {
      pinMode(data_, OUTPUT);
      digitalWrite(data_, txLevel ? HIGH : LOW);
    }
    else
    {
      pinMode(data_, INPUT_PULLUP); // release for turnaround / read data
    }

    delayMicroseconds(half_);
    digitalWrite(clk_, HIGH);
    delayMicroseconds(half_);
    const bool rx = digitalRead(data_); // sample on the high phase
    digitalWrite(clk_, LOW);
    return rx;
  }

  /** Let the bus idle high between transactions. */
  void releaseData() override
  {
    pinMode(data_, INPUT_PULLUP);
  }

private:
  unsigned clk_;
  unsigned data_;
  unsigned half_;
};

// CLK on D18, DATA on D19, 100 us half-bit (~5 kHz). Slow enough for the
// polling CH32V003 peripheral example; use an edge-driven slave for higher rates.
GpioController panel(18, 19, 100);

/**
  * @brief  Set the pump duty and verify the peripheral applied it.
  * @param  tenthsPercent 0..1000 = 0.0..100.0 %
  * @retval true when the read-back matches
  */
bool setPumpDuty(std::uint16_t tenthsPercent)
{
  if (panel.writePumpDuty(tenthsPercent) != Status::Ok)
  {
    return false;
  }

  std::uint16_t applied = 0;
  return panel.readPumpDuty(applied) == Status::Ok && applied == tenthsPercent;
}

/**
  * @brief  Stop the pump if the system has been pressurized for too long.
  */
void pressureGuard()
{
  std::uint32_t pressurizedMs = 0;

  if ((panel.readPressurizedTimeMs(pressurizedMs) == Status::Ok) &&
      (pressurizedMs > 30000U))
  {
    setPumpDuty(0);
  }
}

/**
  * @brief  Open the valve and ramp the pump to 60 %.
  * @note   Blocks for about 3 s; leaves the pump running for the stop button.
  */
void runPumpCycle()
{
  if (panel.writeValveState(ValveState::Open) != Status::Ok)
  {
    return;
  }

  for (std::uint16_t duty = 0; duty <= 600; duty += 10)
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
  panel.begin();

  // Safe state: pump stopped, valve closed.
  setPumpDuty(0);
  panel.writeValveState(ValveState::Closed);
}

void loop()
{
  std::uint8_t buttons = 0;

  if (panel.readButtons(buttons) != Status::Ok)
  {
    delay(100);
    return;
  }

  if ((buttons & kButton1) != 0U)
  {
    runPumpCycle();
  }
  else if ((buttons & kButton2) != 0U)
  {
    setPumpDuty(0);
    panel.writeValveState(ValveState::Closed);
  }

  pressureGuard();
  delay(50);
}
