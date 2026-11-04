/**
  ******************************************************************************
  * @file    peripheral.h
  * @brief   Bit-driven slave engine for the 2-wire bit-banged link.
  *
  * The platform feeds one clocked bit at a time:
  *   - before the clock: drive outputBit() when drivesData() is true
  *   - after the clock:  onBit(sampledData)
  *
  * The engine owns the register file, auto-increment, input sampling and write
  * application. Hardware access goes through PeripheralIo.
  ******************************************************************************
  */

#ifndef REMAGNA_BITBANG_PERIPHERAL_H
#define REMAGNA_BITBANG_PERIPHERAL_H

#include <cstdint>

#include "peripheral_io.h"
#include "register_map.h"

namespace remagna::bitbang
{

class Peripheral
{
public:
  explicit Peripheral(PeripheralIo &io) noexcept;

  Peripheral(const Peripheral &) = delete;
  Peripheral &operator=(const Peripheral &) = delete;

  /** Return to the idle phase without touching the register contents. */
  void reset();

  /** True when the engine is waiting for a START bit. */
  bool isIdle() const;

  /** True when the peripheral must drive DATA for the upcoming clock. */
  bool drivesData() const;

  /** DATA level to drive (valid when drivesData() is true). */
  bool outputBit() const;

  /** Advance one clocked bit with the DATA level sampled by the platform. */
  void onBit(bool dataBit);

private:
  enum class Phase : std::uint8_t
  {
    Idle,
    Command,
    Address,
    Length,
    WriteData,
    Turnaround,
    ReadData,
  };

  void initializeRegisters();
  void sampleInputs();
  void applyWrites();

  std::uint8_t readRegister(std::uint8_t address) const;
  void writeRegister(std::uint8_t address, std::uint8_t value);
  std::uint16_t readRegister16(std::uint8_t address) const;
  void writeRegister16(std::uint8_t address, std::uint16_t value);
  void writeRegister32(std::uint8_t address, std::uint32_t value);

  PeripheralIo &io_;
  std::uint8_t registers_[reg::kMapSize];

  Phase phase_;
  std::uint8_t bitIndex_;
  std::uint8_t command_;
  std::uint8_t address_;
  std::uint8_t length_;
  std::uint8_t remaining_;
  std::uint8_t dataByte_;
  std::uint8_t readByte_;
  std::uint8_t writeStart_;
  std::uint8_t writeLength_;
  bool reading_;
};

} // namespace remagna::bitbang

#endif // REMAGNA_BITBANG_PERIPHERAL_H
