/**
  ******************************************************************************
  * @file    peripheral_io.h
  * @brief   Hardware hooks for the peripheral engine.
  *
  * Implement this on the peripheral platform (e.g. CH32V003 Arduino) to connect
  * the register map to the real display, buttons, battery, ADC, pump and valve.
  ******************************************************************************
  */

#ifndef REMAGNA_BITBANG_PERIPHERAL_IO_H
#define REMAGNA_BITBANG_PERIPHERAL_IO_H

#include <cstdint>

#include "register_map.h"

namespace remagna::bitbang
{

class PeripheralIo
{
public:
  virtual ~PeripheralIo() = default;

  /** bit0..3 = button 1..4, 1 = pressed. */
  virtual std::uint8_t readButtons() = 0;
  virtual bool batteryConnected() = 0;
  virtual std::uint16_t batteryMillivolts() = 0;
  virtual std::uint16_t pressureAdc() = 0;
  virtual std::uint32_t pressurizedTimeMs() = 0;

  virtual void displayWrite(const std::uint8_t digits[reg::kDisplayDigits]) = 0;
  virtual void applyPumpDuty(std::uint16_t duty) = 0;
  virtual void applyValveState(std::uint8_t state) = 0;
};

} // namespace remagna::bitbang

#endif // REMAGNA_BITBANG_PERIPHERAL_IO_H
