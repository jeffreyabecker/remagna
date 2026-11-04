/**
  ******************************************************************************
  * @file    EspController.cpp
  * @brief   Platform side of the bit-banged link on the ESP32-C3.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "EspController.h"

#include <Arduino.h>

EspController::EspController(std::uint8_t clkPin, std::uint8_t dataPin, unsigned halfBitUs)
    : clkPin_(clkPin), dataPin_(dataPin), halfBitUs_(halfBitUs)
{
}

void EspController::begin()
{
  pinMode(clkPin_, OUTPUT);
  digitalWrite(clkPin_, LOW);
  pinMode(dataPin_, INPUT_PULLUP);
}

bool EspController::clockBit(bool drive, bool txLevel)
{
  if (drive)
  {
    pinMode(dataPin_, OUTPUT);
    digitalWrite(dataPin_, txLevel ? HIGH : LOW);
  }
  else
  {
    pinMode(dataPin_, INPUT_PULLUP); // release for turnaround / read data
  }

  delayMicroseconds(halfBitUs_);
  digitalWrite(clkPin_, HIGH);
  delayMicroseconds(halfBitUs_);
  const bool rx = digitalRead(dataPin_) == HIGH; // sample on the high phase
  digitalWrite(clkPin_, LOW);
  return rx;
}

/** Let the bus idle high between transactions. */
void EspController::releaseData()
{
  pinMode(dataPin_, INPUT_PULLUP);
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
