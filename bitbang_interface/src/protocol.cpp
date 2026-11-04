/**
  ******************************************************************************
  * @file    protocol.cpp
  * @brief   Wire definition, register map and both link engines.
  *
  * Controller: host-clocked framing, one bit at a time through clockBit().
  * Peripheral: the slave state machine driven by onBitClocked().
  ******************************************************************************
  */

#include "protocol.h"

namespace remagna::bitbang
{

namespace
{

std::uint16_t littleEndian16(const std::uint8_t raw[2])
{
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(raw[0]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw[1]) << 8));
}

std::uint32_t littleEndian32(const std::uint8_t raw[4])
{
  return static_cast<std::uint32_t>(raw[0]) |
         (static_cast<std::uint32_t>(raw[1]) << 8) |
         (static_cast<std::uint32_t>(raw[2]) << 16) |
         (static_cast<std::uint32_t>(raw[3]) << 24);
}

} // namespace

void Controller::writeByte(std::uint8_t value)
{
  for (std::uint8_t bit = 0U; bit < protocol::BitsPerByte; ++bit)
  {
    (void)clockBit(true, ((value >> bit) & 0x01U) != 0U);
  }
}

std::uint8_t Controller::readByte()
{
  std::uint8_t value = 0U;

  for (std::uint8_t bit = 0U; bit < protocol::BitsPerByte; ++bit)
  {
    if (clockBit(false, false))
    {
      value = static_cast<std::uint8_t>(value | (1U << bit));
    }
  }

  return value;
}

void Controller::readRegister(std::uint8_t address, std::uint8_t *data, std::uint8_t length)
{
  (void)clockBit(true, false); // START
  writeByte(protocol::CommandRead);
  writeByte(address);
  writeByte(length);
  (void)clockBit(false, false); // turnaround

  for (std::uint8_t i = 0U; i < length; ++i)
  {
    data[i] = readByte();
  }

  releaseData();
}

void Controller::writeRegister(std::uint8_t address, const std::uint8_t *data, std::uint8_t length)
{
  (void)clockBit(true, false); // START
  writeByte(protocol::CommandWrite);
  writeByte(address);
  writeByte(length);

  for (std::uint8_t i = 0U; i < length; ++i)
  {
    writeByte(data[i]);
  }

  releaseData();
}

std::uint8_t Controller::deviceId()
{
  std::uint8_t id = 0U;
  readRegister(reg::DeviceId, &id, 1U);
  return id;
}

std::uint8_t Controller::mapVersion()
{
  std::uint8_t version = 0U;
  readRegister(reg::MapVersion, &version, 1U);
  return version;
}

std::array<std::uint8_t, Controller::DisplayDigits> Controller::display()
{
  std::array<std::uint8_t, DisplayDigits> digits{};
  readRegister(reg::Display, digits.data(), static_cast<std::uint8_t>(DisplayDigits));
  return digits;
}

void Controller::writeDisplay(const std::array<std::uint8_t, DisplayDigits> &digits)
{
  writeRegister(reg::Display, digits.data(), static_cast<std::uint8_t>(DisplayDigits));
}

std::uint8_t Controller::buttons()
{
  std::uint8_t mask = 0U;
  readRegister(reg::Buttons, &mask, 1U);
  return mask;
}

bool Controller::batteryConnected()
{
  std::uint8_t status = 0U;
  readRegister(reg::BatteryStatus, &status, 1U);
  return (status & reg::BatteryConnected) != 0U;
}

std::uint16_t Controller::batteryMillivolts()
{
  std::uint8_t raw[2] = {};
  readRegister(reg::BatteryMillivolts, raw, 2U);
  return littleEndian16(raw);
}

std::uint16_t Controller::pressureAdc()
{
  std::uint8_t raw[2] = {};
  readRegister(reg::PressureAdc, raw, 2U);
  return littleEndian16(raw);
}

std::uint16_t Controller::pumpDuty()
{
  std::uint8_t raw[2] = {};
  readRegister(reg::PumpDuty, raw, 2U);
  return littleEndian16(raw);
}

bool Controller::writePumpDuty(std::uint16_t tenthsPercent)
{
  if (tenthsPercent > PumpDutyMax)
  {
    return false;
  }

  const std::uint8_t raw[2] = {
      static_cast<std::uint8_t>(tenthsPercent & 0xFFU),
      static_cast<std::uint8_t>(tenthsPercent >> 8),
  };

  writeRegister(reg::PumpDuty, raw, 2U);
  return true;
}

std::optional<ValveState> Controller::valveState()
{
  std::uint8_t raw = 0U;
  readRegister(reg::ValveState, &raw, 1U);

  if (raw == reg::ValveClosed)
  {
    return ValveState::Closed;
  }

  if (raw == reg::ValveOpen)
  {
    return ValveState::Open;
  }

  return std::nullopt;
}

void Controller::writeValveState(ValveState state)
{
  const std::uint8_t raw = static_cast<std::uint8_t>(state);
  writeRegister(reg::ValveState, &raw, 1U);
}

std::uint32_t Controller::pressurizedTimeMs()
{
  std::uint8_t raw[4] = {};
  readRegister(reg::PressurizedTimeMs, raw, 4U);
  return littleEndian32(raw);
}

Peripheral::Peripheral() noexcept
    : updates_(UpdateNone),
      phase_(Phase::Idle),
      bitIndex_(0U),
      command_(0U),
      address_(0U),
      length_(0U),
      remaining_(0U),
      dataByte_(0U),
      readByte_(0U),
      writeStart_(0U),
      writeLength_(0U),
      reading_(false)
{
  initializeRegisters();
}

void Peripheral::initializeRegisters()
{
  for (std::uint8_t i = 0U; i < reg::MapSize; ++i)
  {
    registers_[i] = 0U;
  }

  registers_[reg::DeviceId] = reg::DeviceIdValue;
  registers_[reg::MapVersion] = reg::MapVersionValue;
  registers_[reg::ValveState] = reg::ValveClosed;

  for (std::uint8_t i = 0U; i < reg::DisplayDigits; ++i)
  {
    registers_[static_cast<std::uint8_t>(reg::Display + i)] = reg::DigitBlank;
  }
}

void Peripheral::reset()
{
  phase_ = Phase::Idle;
  bitIndex_ = 0U;
  command_ = 0U;
  address_ = 0U;
  length_ = 0U;
  remaining_ = 0U;
  dataByte_ = 0U;
  readByte_ = 0U;
  writeStart_ = 0U;
  writeLength_ = 0U;
  reading_ = false;
}

bool Peripheral::isIdle() const
{
  return phase_ == Phase::Idle;
}

Peripheral::Drive Peripheral::onBitClocked(bool clockHigh, bool dataLevel)
{
  if (!clockHigh)
  {
    return dataDrive();
  }

  // DATA keeps its level for the rest of the high phase, so the controller can
  // sample a read bit late in the phase even when this was the final bit.
  onBit(dataLevel);
  return Drive::Hold;
}

Peripheral::Drive Peripheral::dataDrive() const
{
  if (phase_ != Phase::ReadData)
  {
    return Drive::Release;
  }

  return ((readByte_ >> bitIndex_) & 0x01U) != 0U ? Drive::High : Drive::Low;
}

void Peripheral::onBit(bool dataBit)
{
  switch (phase_)
  {
    case Phase::Idle:
      if (dataBit == protocol::StartBit)
      {
        phase_ = Phase::Command;
        bitIndex_ = 0U;
        command_ = 0U;
      }
      break;

    case Phase::Command:
      command_ = static_cast<std::uint8_t>(command_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::BitsPerByte)
      {
        reading_ = (command_ & protocol::CommandReadMask) != 0U;
        phase_ = Phase::Address;
        bitIndex_ = 0U;
        address_ = 0U;
      }
      break;

    case Phase::Address:
      address_ = static_cast<std::uint8_t>(address_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::BitsPerByte)
      {
        phase_ = Phase::Length;
        bitIndex_ = 0U;
        length_ = 0U;
      }
      break;

    case Phase::Length:
      length_ = static_cast<std::uint8_t>(length_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::BitsPerByte)
      {
        if (length_ == 0U)
        {
          phase_ = Phase::Idle;
          break;
        }

        remaining_ = length_;

        if (reading_)
        {
          phase_ = Phase::Turnaround;
        }
        else
        {
          writeStart_ = address_;
          writeLength_ = length_;
          phase_ = Phase::WriteData;
          bitIndex_ = 0U;
          dataByte_ = 0U;
        }
      }
      break;

    case Phase::Turnaround:
      readByte_ = readRegister(address_);
      bitIndex_ = 0U;
      phase_ = Phase::ReadData;
      break;

    case Phase::WriteData:
      dataByte_ = static_cast<std::uint8_t>(dataByte_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::BitsPerByte)
      {
        writeRegister(address_, dataByte_);
        address_ = static_cast<std::uint8_t>(address_ + 1U);
        bitIndex_ = 0U;
        dataByte_ = 0U;

        if (--remaining_ == 0U)
        {
          finishWrite();
          phase_ = Phase::Idle;
        }
      }
      break;

    case Phase::ReadData:
      if (++bitIndex_ == protocol::BitsPerByte)
      {
        address_ = static_cast<std::uint8_t>(address_ + 1U);
        bitIndex_ = 0U;

        if (--remaining_ == 0U)
        {
          phase_ = Phase::Idle;
        }
        else
        {
          readByte_ = readRegister(address_);
        }
      }
      break;
  }
}

void Peripheral::finishWrite()
{
  const std::uint16_t start = writeStart_;
  const std::uint16_t end = static_cast<std::uint16_t>(writeStart_ + writeLength_);

  const auto touched = [start, end](std::uint8_t first, std::uint8_t count)
  {
    return (start < static_cast<std::uint16_t>(first + count)) &&
           (static_cast<std::uint16_t>(first) < end);
  };

  std::uint8_t updates = UpdateNone;

  if (touched(reg::Display, reg::DisplayDigits))
  {
    updates = static_cast<std::uint8_t>(updates | UpdateDisplay);
  }

  if (touched(reg::PumpDuty, 2U))
  {
    if (readRegister16(reg::PumpDuty) > reg::PumpDutyMax)
    {
      writeRegister16(reg::PumpDuty, reg::PumpDutyMax);
    }

    updates = static_cast<std::uint8_t>(updates | UpdatePumpDuty);
  }

  if (touched(reg::ValveState, 1U))
  {
    updates = static_cast<std::uint8_t>(updates | UpdateValveState);
  }

  updates_ = static_cast<std::uint8_t>(updates_ | updates);
}

std::uint8_t Peripheral::takeUpdates()
{
  const std::uint8_t pending = updates_;
  updates_ = static_cast<std::uint8_t>(updates_ & static_cast<std::uint8_t>(~pending));
  return pending;
}

void Peripheral::setButtons(std::uint8_t mask)
{
  writeRegister(reg::Buttons, static_cast<std::uint8_t>(mask & 0x0FU));
}

void Peripheral::setBatteryConnected(bool connected)
{
  writeRegister(reg::BatteryStatus, connected ? reg::BatteryConnected : 0U);
}

void Peripheral::setBatteryMillivolts(std::uint16_t millivolts)
{
  writeRegister16(reg::BatteryMillivolts, millivolts);
}

void Peripheral::setPressureAdc(std::uint16_t counts)
{
  writeRegister16(reg::PressureAdc, counts);
}

void Peripheral::setPressurizedTimeMs(std::uint32_t milliseconds)
{
  writeRegister32(reg::PressurizedTimeMs, milliseconds);
}

std::array<std::uint8_t, Peripheral::DisplayDigits> Peripheral::display() const
{
  std::array<std::uint8_t, DisplayDigits> digits{};

  for (std::size_t i = 0U; i < DisplayDigits; ++i)
  {
    digits[i] = readRegister(static_cast<std::uint8_t>(reg::Display + i));
  }

  return digits;
}

std::uint16_t Peripheral::pumpDuty() const
{
  return readRegister16(reg::PumpDuty);
}

bool Peripheral::valveOpen() const
{
  return readRegister(reg::ValveState) == reg::ValveOpen;
}

std::uint8_t Peripheral::readRegister(std::uint8_t address) const
{
  return registers_[static_cast<std::uint8_t>(address % reg::MapSize)];
}

void Peripheral::writeRegister(std::uint8_t address, std::uint8_t value)
{
  registers_[static_cast<std::uint8_t>(address % reg::MapSize)] = value;
}

std::uint16_t Peripheral::readRegister16(std::uint8_t address) const
{
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(readRegister(address)) |
      static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(readRegister(static_cast<std::uint8_t>(address + 1U))) << 8));
}

void Peripheral::writeRegister16(std::uint8_t address, std::uint16_t value)
{
  writeRegister(address, static_cast<std::uint8_t>(value & 0xFFU));
  writeRegister(static_cast<std::uint8_t>(address + 1U), static_cast<std::uint8_t>(value >> 8));
}

void Peripheral::writeRegister32(std::uint8_t address, std::uint32_t value)
{
  writeRegister(address, static_cast<std::uint8_t>(value & 0xFFU));
  writeRegister(static_cast<std::uint8_t>(address + 1U), static_cast<std::uint8_t>((value >> 8) & 0xFFU));
  writeRegister(static_cast<std::uint8_t>(address + 2U), static_cast<std::uint8_t>((value >> 16) & 0xFFU));
  writeRegister(static_cast<std::uint8_t>(address + 3U), static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

} // namespace remagna::bitbang
