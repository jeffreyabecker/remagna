#ifndef CH32_CONSTANTS_H
#define CH32_CONSTANTS_H
#include <cstdint>
constexpr uint8_t PIN_CLK = 2;
constexpr uint8_t PIN_DATA = 3;

constexpr uint8_t PIN_BUTTON1 = 4;
constexpr uint8_t PIN_BUTTON2 = 5;
constexpr uint8_t PIN_BUTTON3 = 6;
constexpr uint8_t PIN_BUTTON4 = 7;

constexpr uint8_t PIN_BATTERY_PRESENT = 8;

// Only A1 and A2 are ADC-capable on the CH32V003, and the Arduino core names
// them A1/A2 (there is no A0).
constexpr uint8_t PIN_BATTERY_ADC = A1;
constexpr uint8_t PIN_PRESSURE_ADC = A2;
constexpr uint8_t PIN_PUMP = 9;
constexpr uint8_t PIN_VALVE = 10;

// Battery divider: Vbat -- R1 --+-- R2 -- GND, ADC at the tap.
constexpr uint32_t BatteryAdcFullScale = 1023U; // 10-bit ADC
constexpr uint32_t BatteryAdcRefMillivolts = 3300U;
constexpr uint32_t BatteryDividerRatio = 11U;   // (R1 + R2) / R2

// Pressure ADC count that means "pressurized"; tune to the sensor.
constexpr std::uint16_t PressureThreshold = 512U;

// Abort a partial frame if the controller stops clocking for this long.
constexpr std::uint32_t BusIdleTimeoutUs = 10000U;

#endif // CH32_CONSTANTS_H