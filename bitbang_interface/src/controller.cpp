/**
  ******************************************************************************
  * @file    controller.cpp
  * @brief   Controller-side driver for the 2-wire bit-banged peripheral.
  *
  * Frame (host-driven, LSB-first):
  *   START    1 bit, host drives 0
  *   COMMAND  8 bits, bit0 = R/W (0 = write, 1 = read)
  *   ADDRESS  8 bits, first register offset
  *   LENGTH   8 bits, number of data bytes (1..255)
  *   write:   DATA, host drives
  *   read:    TURN (data released) + DATA, device drives
  ******************************************************************************
  */

#include "controller.h"
#include "protocol.h"

namespace remagna::bitbang
{
void Controller::writeByte(std::uint8_t value)
{
  for (std::uint8_t bit = 0U; bit < protocol::kBitsPerByte; ++bit)
  {
    (void)clockBit(true, ((value >> bit) & 0x01U) != 0U);
  }
}

std::uint8_t Controller::readByte()
{
  std::uint8_t value = 0U;

  for (std::uint8_t bit = 0U; bit < protocol::kBitsPerByte; ++bit)
  {
    if (clockBit(false, false))
    {
      value = static_cast<std::uint8_t>(value | (1U << bit));
    }
  }

  return value;
}

Status Controller::read(std::uint8_t address, std::uint8_t *data, std::uint8_t length)
{
  if ((data == nullptr) || (length == 0U))
  {
    return Status::InvalidArgument;
  }

  (void)clockBit(true, false); // START
  writeByte(protocol::kCommandRead);
  writeByte(address);
  writeByte(length);
  (void)clockBit(false, false); // turnaround

  for (std::uint8_t i = 0U; i < length; ++i)
  {
    data[i] = readByte();
  }

  releaseData();
  return Status::Ok;
}

Status Controller::write(std::uint8_t address, const std::uint8_t *data, std::uint8_t length)
{
  if ((data == nullptr) || (length == 0U))
  {
    return Status::InvalidArgument;
  }

  (void)clockBit(true, false); // START
  writeByte(protocol::kCommandWrite);
  writeByte(address);
  writeByte(length);

  for (std::uint8_t i = 0U; i < length; ++i)
  {
    writeByte(data[i]);
  }

  releaseData();
  return Status::Ok;
}

Status Controller::readDeviceId(std::uint8_t &id)
{
  return read(reg::kDeviceId, &id, 1U);
}

Status Controller::readMapVersion(std::uint8_t &version)
{
  return read(reg::kMapVersion, &version, 1U);
}

Status Controller::readDisplay(std::array<std::uint8_t, kDisplayDigits> &digits)
{
  return read(reg::kDisplay, digits.data(), static_cast<std::uint8_t>(kDisplayDigits));
}

Status Controller::writeDisplay(const std::array<std::uint8_t, kDisplayDigits> &digits)
{
  return write(reg::kDisplay, digits.data(), static_cast<std::uint8_t>(kDisplayDigits));
}

Status Controller::readButtons(std::uint8_t &mask)
{
  return read(reg::kButtons, &mask, 1U);
}

Status Controller::readBatteryConnected(bool &connected)
{
  std::uint8_t status = 0U;
  const Status result = read(reg::kBatteryStatus, &status, 1U);

  if (result == Status::Ok)
  {
    connected = (status & reg::kBatteryConnected) != 0U;
  }

  return result;
}

Status Controller::readBatteryMillivolts(std::uint16_t &millivolts)
{
  std::uint8_t raw[2] = {};
  const Status result = read(reg::kBatteryMillivolts, raw, 2U);

  if (result == Status::Ok)
  {
    millivolts = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(raw[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw[1]) << 8));
  }

  return result;
}

Status Controller::readPressureAdc(std::uint16_t &counts)
{
  std::uint8_t raw[2] = {};
  const Status result = read(reg::kPressureAdc, raw, 2U);

  if (result == Status::Ok)
  {
    counts = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(raw[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw[1]) << 8));
  }

  return result;
}

Status Controller::readPumpDuty(std::uint16_t &tenthsPercent)
{
  std::uint8_t raw[2] = {};
  const Status result = read(reg::kPumpDuty, raw, 2U);

  if (result == Status::Ok)
  {
    tenthsPercent = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(raw[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw[1]) << 8));
  }

  return result;
}

Status Controller::writePumpDuty(std::uint16_t tenthsPercent)
{
  if (tenthsPercent > kPumpDutyMax)
  {
    return Status::InvalidArgument;
  }

  const std::uint8_t raw[2] = {
      static_cast<std::uint8_t>(tenthsPercent & 0xFFU),
      static_cast<std::uint8_t>(tenthsPercent >> 8),
  };

  return write(reg::kPumpDuty, raw, 2U);
}

Status Controller::readValveState(ValveState &state)
{
  std::uint8_t raw = 0U;
  const Status result = read(reg::kValveState, &raw, 1U);

  if (result != Status::Ok)
  {
    return result;
  }

  if (raw == reg::kValveClosed)
  {
    state = ValveState::Closed;
    return Status::Ok;
  }

  if (raw == reg::kValveOpen)
  {
    state = ValveState::Open;
    return Status::Ok;
  }

  return Status::InvalidArgument;
}

Status Controller::writeValveState(ValveState state)
{
  const std::uint8_t raw = static_cast<std::uint8_t>(state);
  return write(reg::kValveState, &raw, 1U);
}

Status Controller::readPressurizedTimeMs(std::uint32_t &milliseconds)
{
  std::uint8_t raw[4] = {};
  const Status result = read(reg::kPressurizedTimeMs, raw, 4U);

  if (result == Status::Ok)
  {
    milliseconds = static_cast<std::uint32_t>(raw[0]) |
                   (static_cast<std::uint32_t>(raw[1]) << 8) |
                   (static_cast<std::uint32_t>(raw[2]) << 16) |
                   (static_cast<std::uint32_t>(raw[3]) << 24);
  }

  return result;
}

} // namespace remagna::bitbang
