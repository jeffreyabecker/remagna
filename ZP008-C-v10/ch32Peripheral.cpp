/**
  ******************************************************************************
  * @file    ch32Peripheral.cpp
  * @brief   The magna peripheral on the ZP008-C-v10 board (CH32V003F4P6).
  *
  * The ISR does no hardware work beyond driving DATA, so the bus can run much
  * faster than a polled loop allows. Only the main loop touches the ADC, PWM
  * and the panel.
  *
  * Wiring:
  *   CLK   <- controller CLK
  *   DATA  <-> controller DATA (open-drain / pulled up)
  *   GND common
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "ch32Peripheral.h"

#include <Arduino.h>

#include "ch32_constants.h"

void ch32Peripheral::setup()
{
  pinMode(PIN_CLK, INPUT);
  pinMode(PIN_DATA, INPUT_PULLUP);

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON4, INPUT_PULLUP);
  pinMode(PIN_BATTERY_PRESENT, INPUT_PULLUP);

  pinMode(PIN_VALVE, OUTPUT);
  digitalWrite(PIN_VALVE, LOW);

  pinMode(PIN_PUMP, OUTPUT);
  analogWrite(PIN_PUMP, 0);

  lastEdgeUs_ = micros();
  lastPressureSampleMs_ = millis();
  appliedDrive_ = Drive::Release;
  publishInputs(analogRead(PIN_PRESSURE_ADC));
}

void ch32Peripheral::loop()
{
  abortStalledFrame(micros());

  const std::uint16_t pressureAdc = analogRead(PIN_PRESSURE_ADC);
  updatePressure(pressureAdc);
  publishInputs(pressureAdc);
}

void ch32Peripheral::onClockEdge()
{
  const bool clockHigh = digitalRead(PIN_CLK) == HIGH;
  const bool dataLevel = digitalRead(PIN_DATA) == HIGH;

  lastEdgeUs_ = micros();

  const Drive target = onBitClocked(clockHigh, dataLevel);

  if ((target == Drive::Hold) || (target == appliedDrive_))
  {
    return; // keep the current DATA state, e.g. mid read bit
  }
  appliedDrive_ = target;

  if (target == Drive::Release)
  {
    pinMode(PIN_DATA, INPUT_PULLUP);
    return;
  }

  pinMode(PIN_DATA, OUTPUT);
  digitalWrite(PIN_DATA, (target == Drive::High) ? HIGH : LOW);
}

/** Drop a partial frame when the controller stops clocking mid-transfer. */
void ch32Peripheral::abortStalledFrame(std::uint32_t nowUs)
{
  if (!isIdle() && ((nowUs - lastEdgeUs_) > BusIdleTimeoutUs))
  {
    noInterrupts();
    reset();
    interrupts();
    pinMode(PIN_DATA, INPUT_PULLUP);
    appliedDrive_ = Drive::Release;
  }
}

/** Accumulate the time the system spends pressurized. */
void ch32Peripheral::updatePressure(std::uint16_t pressureAdc)
{
  const std::uint32_t now = millis();
  const std::uint32_t elapsed = now - lastPressureSampleMs_;
  lastPressureSampleMs_ = now;

  if (pressureAdc >= PressureThreshold)
  {
    pressurizedMs_ += elapsed;
  }
  else
  {
    pressurizedMs_ = 0U;
  }
}

void ch32Peripheral::publishInputs(std::uint16_t pressureAdc)
{
  setButtons(readButtons());
  setBatteryConnected(digitalRead(PIN_BATTERY_PRESENT) == LOW); // adjust polarity
  setBatteryMillivolts(batteryMillivolts());
  setPressureAdc(pressureAdc);
  setPressurizedTimeMs(pressurizedMs_);
}

/** Apply whatever the controller wrote since the last call. */
std::uint8_t ch32Peripheral::readButtons()
{
  return build(digitalRead(PIN_BUTTON1) == LOW,
               digitalRead(PIN_BUTTON2) == LOW,
               digitalRead(PIN_BUTTON3) == LOW,
               digitalRead(PIN_BUTTON4) == LOW);
}

std::uint16_t ch32Peripheral::batteryMillivolts()
{
  const std::uint32_t counts = static_cast<std::uint32_t>(analogRead(PIN_BATTERY_ADC));
  return static_cast<std::uint16_t>(
      (counts * BatteryAdcRefMillivolts * BatteryDividerRatio) / BatteryAdcFullScale);
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
