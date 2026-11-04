/**
  ******************************************************************************
  * @file    Py32Peripheral.h
  * @brief   The magna peripheral on ZP008 rev 10.
  *
  * Py32Peripheral is the register file and frame engine of the bit-banged link
  * (bitbang_interface/src/protocol.h) plus the two things that engine leaves to
  * the platform: the CLK/DATA pins, including the CLK edge interrupt, and the
  * mapping between the registers and the board I/O.
  ******************************************************************************
  */

#ifndef __PY32PERIPHERAL_H
#define __PY32PERIPHERAL_H

/* Includes ------------------------------------------------------------------*/
#include <array>
#include <cstddef>
#include <cstdint>

#include "protocol.h"

class Py32Peripheral final : public Peripheral
{
public:
  /** Number of user buttons on the panel. */
  static constexpr std::size_t ButtonCount = 4U;

  Py32Peripheral() = default;

  Py32Peripheral(const Py32Peripheral &) = delete;
  Py32Peripheral &operator=(const Py32Peripheral &) = delete;

  /** Configure CLK/DATA and enable the CLK edge interrupt. */
  void init();

  /** Advance the engine by one CLK edge; call from the CLK interrupt. */
  void onClockEdge();

  /**
    * @brief  Publish the inputs, apply the written outputs, drop stalled frames.
    * @note   Call from the main loop; never blocks.
    */
  void poll();

private:
  /* Register file <-> board I/O. */
  void publishInputs();
  void applyOutputs();

  void displayWrite(const std::array<std::uint8_t, DisplayDigits> &digits);
  std::array<bool, ButtonCount> readButtons();
  bool batteryConnected();
  std::uint16_t batteryMillivolts();
  std::uint16_t pressureAdc();
  void applyMotorDuty(std::uint16_t tenthsPercent);
  void applyValveState(bool open);

  /* DATA line handling. */
  void releaseData();
  void driveData(bool high);

  /* DATA state the interrupt has applied, so unchanged levels skip the writes. */
  Drive appliedDrive_ = Drive::Release;

  std::uint32_t pressureSampleMs_ = 0U;
  std::uint32_t pressurizedMs_ = 0U;

  /* Written by the CLK interrupt, read by the main loop. */
  volatile std::uint32_t lastEdgeMs_ = 0U;
};

#endif /* __PY32PERIPHERAL_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
