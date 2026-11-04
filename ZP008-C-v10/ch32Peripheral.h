/**
  ******************************************************************************
  * @file    ch32Peripheral.h
  * @brief   The magna peripheral on the ZP008-C-v10 board (CH32V003F4P6).
  *
  * The bitbang_interface Peripheral engine owns the register file and the frame
   * state machine; this class owns the input side of the board around it:
  *   - setup() configures every pin, the PWM and the initial input values,
   *   - loop() drops stalled frames, keeps the pressure time and publishes the
   *     inputs,
  *   - the CLK interrupt the sketch installs feeds one edge at a time to the
  *     engine through onClockEdge(), which drives DATA with the state it
  *     returns.
   *
   * The output registers are left to the sketch: it polls takeUpdates() and
   * drives the panel, pump and valve itself.
  ******************************************************************************
  */

#ifndef CH32PERIPHERAL_H
#define CH32PERIPHERAL_H

/* Includes ------------------------------------------------------------------*/
#include <cstdint>

#include <protocol.h>

class ch32Peripheral final : public Peripheral
{
public:
  /** Board init: pins, PWM and the initial input values. */
  void setup();

  /** Per-pass work: stalled frames, pressure time and the input registers. */
  void loop();

  /** Feed one CLK edge to the engine and drive DATA; call from the interrupt. */
  void onClockEdge();

private:
  void abortStalledFrame(std::uint32_t nowUs);
  void updatePressure(std::uint16_t pressureAdc);
  void publishInputs(std::uint16_t pressureAdc);

  std::uint8_t readButtons();
  std::uint16_t batteryMillivolts();

  /* DATA state the interrupt has applied, so unchanged levels skip the writes. */
  Drive appliedDrive_ = Drive::Release;

  std::uint32_t pressurizedMs_ = 0U;
  std::uint32_t lastPressureSampleMs_ = 0U;
  std::uint32_t lastEdgeUs_ = 0U;
};

#endif // CH32PERIPHERAL_H

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
