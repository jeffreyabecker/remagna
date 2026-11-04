/**
  ******************************************************************************
  * @file    BleController.h
  * @brief   BLE front end for the link: one GATT service mirroring the register
  *          map.
  *
  * The service is advertised as "magna-controller":
  *
  *   service fff0
  *     fff1  device info  read        [0] device id, [1] map version
  *     fff2  buttons      read        [0] bit0..3 = button 1..4, 1 = pressed
  *     fff3  battery      read        [0] connected, [1..2] millivolts, LE
  *     fff4  pressure     read        [0..1] ADC counts, [2..5] pressurized ms, LE
  *     fff5  pump duty    read/write  [0..1] tenths of a percent (0..1000), LE
  *     fff6  valve        read/write  [0] 0 = closed, 1 = open
  *     fff7  display      read/write  [0..5] digit values, 0x00..0x0F, 0x10 blank
  *
  * Writes never touch the link: a transaction takes milliseconds and the
  * callbacks run in the NimBLE host task, so the client's writes are queued and
  * handed back by takeWrites() for the main loop to apply.
  *
  * NimBLE stays an implementation detail of BleController.cpp; this header only
  * sees the register-map types.
  ******************************************************************************
  */

#ifndef BLECONTROLLER_H
#define BLECONTROLLER_H

/* Includes ------------------------------------------------------------------*/
#include <cstdint>

#include <protocol.h>

/** Link writes the client requested since the last takeWrites(). */
struct BleWrites
{
  bool pumpDuty = false;
  uint16_t pumpDutyValue = 0U;
  bool valve = false;
  bool valveOpen = false;
  bool display = false;
  char displayDigits[DisplayDigits] = {};
};

class BleController
{
public:
  /** Start the BLE stack, publish the service and start advertising. */
  void begin();

  /* Read-only characteristics, refreshed from the link by the main loop. */
  void publishDeviceInfo(uint8_t deviceId, uint8_t mapVersion);
  void publishButtons(uint8_t mask);
  void publishBattery(bool connected, uint16_t millivolts);
  void publishPressure(uint16_t adc, uint32_t pressurizedMilliseconds);

  /* Output characteristics, refreshed after the link applied a write. */
  void publishPumpDuty(uint16_t tenthsPercent);
  void publishValve(bool open);
  void publishDisplay(const char digits[DisplayDigits]);

  /** Return the queued writes and clear the queue. */
  BleWrites takeWrites();
};

#endif // BLECONTROLLER_H

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
