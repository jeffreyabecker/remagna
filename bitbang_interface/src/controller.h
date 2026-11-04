/**
  ******************************************************************************
  * @file    controller.h
  * @brief   Controller-side driver for the 2-wire bit-banged peripheral.
  *
  * The class owns framing (start, command, address, length, data) and exposes
  * the device features. GPIO access and bit timing are implemented by the
  * platform in the derived class through clockBit() and releaseData().
  ******************************************************************************
  */

#ifndef REMAGNA_BITBANG_CONTROLLER_H
#define REMAGNA_BITBANG_CONTROLLER_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "register_map.h"

namespace remagna::bitbang
{

enum class Status : std::uint8_t
{
  Ok = 0,
  InvalidArgument,
};

enum class ValveState : std::uint8_t
{
  Closed = reg::kValveClosed,
  Open = reg::kValveOpen,
};

/**
  * @brief  Controller API for the bit-banged peripheral.
  *
  * The platform owns the two GPIOs (CLOCK and DATA), the pin directions and all
  * timing; the driver only asks it to clock one bit at a time.
  *
  * All transfers are host-clocked and LSB-first. Reads release the data line
  * for one turnaround bit before the device drives the result. Calls are
  * blocking and not thread-safe.
  */
class Controller
{
public:
  static constexpr std::size_t kDisplayDigits = reg::kDisplayDigits;
  static constexpr std::uint16_t kPumpDutyMax = reg::kPumpDutyMax;

  Controller() = default;
  virtual ~Controller() = default;

  Controller(const Controller &) = delete;
  Controller &operator=(const Controller &) = delete;

  /** Raw register access; the address auto-increments across length bytes. */
  Status read(std::uint8_t address, std::uint8_t *data, std::uint8_t length);
  Status write(std::uint8_t address, const std::uint8_t *data, std::uint8_t length);

  Status readDeviceId(std::uint8_t &id);
  Status readMapVersion(std::uint8_t &version);

  /** Digit values: 0x00..0x0F show 0..F, reg::kDigitBlank blanks a digit. */
  Status readDisplay(std::array<std::uint8_t, kDisplayDigits> &digits);
  Status writeDisplay(const std::array<std::uint8_t, kDisplayDigits> &digits);

  /** bit0..3 = button 1..4, 1 = pressed. */
  Status readButtons(std::uint8_t &mask);

  Status readBatteryConnected(bool &connected);
  Status readBatteryMillivolts(std::uint16_t &millivolts);
  Status readPressureAdc(std::uint16_t &counts);

  /** Duty in tenths of a percent: 0..1000 = 0.0..100.0 %. */
  Status readPumpDuty(std::uint16_t &tenthsPercent);
  Status writePumpDuty(std::uint16_t tenthsPercent);

  Status readValveState(ValveState &state);
  Status writeValveState(ValveState state);

  Status readPressurizedTimeMs(std::uint32_t &milliseconds);

protected:
  /**
    * @brief  Clock exactly one bit, implemented by each platform.
    *
    * Semantics for each call:
    *  - drive == true : drive txLevel on DATA, pulse CLOCK, return the DATA level
    *                    sampled around the clock edge (the device is not expected
    *                    to drive while the host drives).
    *  - drive == false: release DATA (input with pull-up), pulse CLOCK, return the
    *                    DATA level driven by the device.
    *
    * The clock idles low and bits are transferred LSB-first. The platform decides
    * the edge order and delay; the driver never touches GPIOs directly.
    */
  virtual bool clockBit(bool drive, bool txLevel) = 0;

  /**
    * @brief  Return DATA to input/pull-up when the bus is idle.
    *
    * Called once at the end of every read/write transaction. The peripheral
    * uses the idle-high level to detect the next START bit.
    */
  virtual void releaseData() = 0;

private:
  void writeByte(std::uint8_t value);
  std::uint8_t readByte();
};

} // namespace remagna::bitbang

#endif // REMAGNA_BITBANG_CONTROLLER_H
