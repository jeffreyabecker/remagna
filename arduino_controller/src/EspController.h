/**
  ******************************************************************************
  * @file    EspController.h
  * @brief   Platform side of the bit-banged link on the ESP32-C3.
  *
  * Implements the two hooks Controller leaves to the platform: clockBit(), which
  * drives DATA, pulses CLK and samples the level the peripheral drives back, and
  * releaseData(). The frame format, the register map and the transaction helpers
  * come from the bitbang_interface library.
  ******************************************************************************
  */

#ifndef ESPCONTROLLER_H
#define ESPCONTROLLER_H

/* Includes ------------------------------------------------------------------*/
#include <cstdint>

#include <protocol.h>

class EspController final : public Controller
{
public:
  /**
    * @brief  Bind the link to two GPIOs.
    * @param  clkPin     CLK output, driven by this device.
    * @param  dataPin    DATA, driven by this device for writes and released for
    *                    turnaround and reads.
    * @param  halfBitUs  Time CLK stays low and high per bit; the peripherals are
    *                    edge driven, so a few microseconds upwards all work.
    */
  EspController(std::uint8_t clkPin, std::uint8_t dataPin, unsigned halfBitUs);

  /** Configure the pins and leave the bus idle (CLK low, DATA released). */
  void begin();

protected:
  bool clockBit(bool drive, bool txLevel) override;
  void releaseData() override;

private:
  std::uint8_t clkPin_;
  std::uint8_t dataPin_;
  unsigned halfBitUs_;
};

#endif // ESPCONTROLLER_H

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
