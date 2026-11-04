/**
  ******************************************************************************
  * @file    register_map.h
  * @brief   Register map exposed over the 2-wire bit-banged link.
  *
  * Multi-byte values are little-endian and auto-increment across a transfer.
  *
  *   0x00  DEVICE_ID            R    ASCII 'M'
  *   0x01  MAP_VERSION          R    register map version
  *   0x02  DISPLAY_DIGIT1       R/W  0x00..0x0F = 0..F, 0x10 = blank
  *   0x03  DISPLAY_DIGIT2       R/W
  *   0x04  DISPLAY_DIGIT3       R/W
  *   0x05  DISPLAY_DIGIT4       R/W
  *   0x06  DISPLAY_DIGIT5       R/W
  *   0x07  DISPLAY_DIGIT6       R/W
  *   0x08  BUTTONS              R    bit0..3 = button 1..4, 1 = pressed
  *   0x09  BATTERY_STATUS       R    bit0 = battery connected
  *   0x0A  BATTERY_MV_L         R    battery voltage in mV
  *   0x0B  BATTERY_MV_H         R
  *   0x0C  PRESSURE_ADC_L       R    raw pressure ADC counts
  *   0x0D  PRESSURE_ADC_H       R
  *   0x0E  PUMP_DUTY_L          R/W  pump duty, 0..1000 = 0.0..100.0 %
  *   0x0F  PUMP_DUTY_H          R/W
  *   0x10  VALVE_STATE          R/W  0 = closed, 1 = open
  *   0x11  PRESSURIZED_TIME_MS  R    milliseconds since pressurised, u32
  *   0x12  PRESSURIZED_TIME_MS+1
  *   0x13  PRESSURIZED_TIME_MS+2
  *   0x14  PRESSURIZED_TIME_MS+3
  ******************************************************************************
  */

#ifndef REMAGNA_BITBANG_REGISTER_MAP_H
#define REMAGNA_BITBANG_REGISTER_MAP_H

#include <cstdint>

namespace remagna::bitbang::reg
{

inline constexpr std::uint8_t kDeviceId = 0x00U;
inline constexpr std::uint8_t kMapVersion = 0x01U;
inline constexpr std::uint8_t kDisplay = 0x02U;
inline constexpr std::uint8_t kButtons = 0x08U;
inline constexpr std::uint8_t kBatteryStatus = 0x09U;
inline constexpr std::uint8_t kBatteryMillivolts = 0x0AU;
inline constexpr std::uint8_t kPressureAdc = 0x0CU;
inline constexpr std::uint8_t kPumpDuty = 0x0EU;
inline constexpr std::uint8_t kValveState = 0x10U;
inline constexpr std::uint8_t kPressurizedTimeMs = 0x11U;

inline constexpr std::uint8_t kMapSize = 0x15U;

inline constexpr std::uint8_t kDeviceIdValue = 0x4DU; // 'M'
inline constexpr std::uint8_t kMapVersionValue = 0x01U;

inline constexpr std::uint8_t kDisplayDigits = 6U;
inline constexpr std::uint8_t kDigitBlank = 0x10U;

inline constexpr std::uint8_t kButton1 = 0x01U;
inline constexpr std::uint8_t kButton2 = 0x02U;
inline constexpr std::uint8_t kButton3 = 0x04U;
inline constexpr std::uint8_t kButton4 = 0x08U;

inline constexpr std::uint8_t kBatteryConnected = 0x01U;

inline constexpr std::uint16_t kPumpDutyMax = 1000U;

inline constexpr std::uint8_t kValveClosed = 0x00U;
inline constexpr std::uint8_t kValveOpen = 0x01U;

} // namespace remagna::bitbang::reg

#endif // REMAGNA_BITBANG_REGISTER_MAP_H
