/**
  ******************************************************************************
  * @file    peripheral.cpp
  * @brief   Bit-driven slave engine for the 2-wire bit-banged link.
  ******************************************************************************
  */

#include "peripheral.h"
#include "protocol.h"

namespace remagna::bitbang
{

Peripheral::Peripheral(PeripheralIo &io) noexcept
    : io_(io),
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
  for (std::uint8_t i = 0U; i < reg::kMapSize; ++i)
  {
    registers_[i] = 0U;
  }

  registers_[reg::kDeviceId] = reg::kDeviceIdValue;
  registers_[reg::kMapVersion] = reg::kMapVersionValue;
  registers_[reg::kValveState] = reg::kValveClosed;

  for (std::uint8_t i = 0U; i < reg::kDisplayDigits; ++i)
  {
    registers_[static_cast<std::uint8_t>(reg::kDisplay + i)] = reg::kDigitBlank;
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

bool Peripheral::drivesData() const
{
  return phase_ == Phase::ReadData;
}

bool Peripheral::outputBit() const
{
  if (phase_ != Phase::ReadData)
  {
    return false;
  }

  return ((readByte_ >> bitIndex_) & 0x01U) != 0U;
}

void Peripheral::onBit(bool dataBit)
{
  switch (phase_)
  {
    case Phase::Idle:
      if (dataBit == protocol::kStartBit)
      {
        phase_ = Phase::Command;
        bitIndex_ = 0U;
        command_ = 0U;
      }
      break;

    case Phase::Command:
      command_ = static_cast<std::uint8_t>(command_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::kBitsPerByte)
      {
        reading_ = (command_ & protocol::kCommandReadMask) != 0U;
        phase_ = Phase::Address;
        bitIndex_ = 0U;
        address_ = 0U;
      }
      break;

    case Phase::Address:
      address_ = static_cast<std::uint8_t>(address_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::kBitsPerByte)
      {
        phase_ = Phase::Length;
        bitIndex_ = 0U;
        length_ = 0U;
      }
      break;

    case Phase::Length:
      length_ = static_cast<std::uint8_t>(length_ | ((dataBit ? 1U : 0U) << bitIndex_));
      if (++bitIndex_ == protocol::kBitsPerByte)
      {
        if (length_ == 0U)
        {
          phase_ = Phase::Idle;
          break;
        }

        remaining_ = length_;

        if (reading_)
        {
          sampleInputs();
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
      if (++bitIndex_ == protocol::kBitsPerByte)
      {
        writeRegister(address_, dataByte_);
        address_ = static_cast<std::uint8_t>(address_ + 1U);
        bitIndex_ = 0U;
        dataByte_ = 0U;

        if (--remaining_ == 0U)
        {
          applyWrites();
          phase_ = Phase::Idle;
        }
      }
      break;

    case Phase::ReadData:
      if (++bitIndex_ == protocol::kBitsPerByte)
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

void Peripheral::sampleInputs()
{
  writeRegister(reg::kButtons, static_cast<std::uint8_t>(io_.readButtons() & 0x0FU));
  writeRegister(reg::kBatteryStatus, io_.batteryConnected() ? reg::kBatteryConnected : 0U);
  writeRegister16(reg::kBatteryMillivolts, io_.batteryMillivolts());
  writeRegister16(reg::kPressureAdc, io_.pressureAdc());
  writeRegister32(reg::kPressurizedTimeMs, io_.pressurizedTimeMs());
}

void Peripheral::applyWrites()
{
  const std::uint16_t start = writeStart_;
  const std::uint16_t end = static_cast<std::uint16_t>(writeStart_ + writeLength_);

  const auto touched = [start, end](std::uint8_t first, std::uint8_t count)
  {
    return (start < static_cast<std::uint16_t>(first + count)) &&
           (static_cast<std::uint16_t>(first) < end);
  };

  if (touched(reg::kDisplay, reg::kDisplayDigits))
  {
    io_.displayWrite(&registers_[reg::kDisplay]);
  }

  if (touched(reg::kPumpDuty, 2U))
  {
    std::uint16_t duty = readRegister16(reg::kPumpDuty);

    if (duty > reg::kPumpDutyMax)
    {
      duty = reg::kPumpDutyMax;
      writeRegister16(reg::kPumpDuty, duty);
    }

    io_.applyPumpDuty(duty);
  }

  if (touched(reg::kValveState, 1U))
  {
    io_.applyValveState(readRegister(reg::kValveState));
  }
}

std::uint8_t Peripheral::readRegister(std::uint8_t address) const
{
  return registers_[static_cast<std::uint8_t>(address % reg::kMapSize)];
}

void Peripheral::writeRegister(std::uint8_t address, std::uint8_t value)
{
  registers_[static_cast<std::uint8_t>(address % reg::kMapSize)] = value;
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
