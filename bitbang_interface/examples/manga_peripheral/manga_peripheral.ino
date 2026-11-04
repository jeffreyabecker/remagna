/**
  ******************************************************************************
  * @file    manga_peripheral.ino
  * @brief   Arduino sketch: PERIPHERAL device for the manga machine.
  *
  * Target: CH32V003F4P6 (TSSOP20, 16 KB flash / 2 KB SRAM) running the
  * CH32V003 Arduino core. Adjust the pin defines to your board package.
  *
  * The protocol engine and register file come from the bitbang_interface
  * library (remagna::bitbang::Peripheral). This sketch only supplies:
  *   - ArduinoPeripheralIo: the hardware hooks (buttons, battery, ADC, display,
  *     pump, valve, pressurized time)
  *   - Tm1652Display: pushes the digit registers to the TM1652 over UART
  *   - CLK edge polling that feeds the engine one bit at a time
  *
  * Wiring:
  *   CLK   <- controller CLK
  *   DATA  <-> controller DATA (open-drain / pulled up)
  *   DIN   <- USART1 TX (TM1652 display, 19200 8E1)
  *   GND common
  *
  * This sketch polls CLK edges, so keep the controller at a few kHz (the
  * controller example uses a 100 us half-bit, ~5 kHz). For higher rates, move
  * the edge handling into interrupts and call the same onBit()/outputBit().
  ******************************************************************************
  */

#include <Arduino.h>
#include <peripheral.h>
#include <register_map.h>

namespace rb = remagna::bitbang;

// ---------------------------------------------------------------------------
// Pin assignment (CH32V003F4P6; change to match your board)
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_CLK = 2;
constexpr uint8_t PIN_DATA = 3;

constexpr uint8_t PIN_BUTTON1 = 4;
constexpr uint8_t PIN_BUTTON2 = 5;
constexpr uint8_t PIN_BUTTON3 = 6;
constexpr uint8_t PIN_BUTTON4 = 7;

constexpr uint8_t PIN_BATTERY_PRESENT = 8;
constexpr uint8_t PIN_BATTERY_ADC = A0;
constexpr uint8_t PIN_PRESSURE_ADC = A1;
constexpr uint8_t PIN_PUMP = 9;
constexpr uint8_t PIN_VALVE = 10;

// Battery divider: Vbat -- R1 --+-- R2 -- GND, ADC at the tap.
constexpr uint32_t kBatteryAdcFullScale = 1023U; // 10-bit ADC
constexpr uint32_t kBatteryAdcRefMillivolts = 3300U;
constexpr uint32_t kBatteryDividerRatio = 11U;   // (R1 + R2) / R2

// Pressure ADC count that means "pressurized"; tune to the sensor.
constexpr std::uint16_t kPressureThreshold = 512U;

// Abort a partial frame if the controller stops clocking for this long.
constexpr std::uint32_t kBusIdleTimeoutMs = 10U;

// ---------------------------------------------------------------------------
// 6-digit display: TM1652 driven over a single-wire UART
//
// The TM1652 wants 19200 baud, 8 data bits (LSB first), even parity and one
// stop bit - exactly SERIAL_8E1. Consecutive commands must be separated by at
// least 3 ms of a high data line. The address command auto-increments, so one
// 0x08 command followed by six segment bytes refreshes every digit.
// ---------------------------------------------------------------------------
constexpr std::uint32_t kDisplayBaud = 19200U;
constexpr std::uint32_t kDisplayCommandGapUs = 3000U;
constexpr std::uint8_t kDisplayCommandMode = 0x18U;
constexpr std::uint8_t kDisplayCommandAddress = 0x08U;

// The UART burst blocks the polling loop for a few milliseconds, so only start
// it once the link has been quiet this long.
constexpr std::uint32_t kDisplayQuietMs = 5U;

constexpr std::uint8_t kDisplayIntensity = 4U;    // 0..7
constexpr std::uint8_t kDisplayDriveCurrent = 6U; // 0..6

constexpr std::uint8_t reverseBits(std::uint8_t value)
{
  std::uint8_t result = 0U;

  for (std::uint8_t bit = 0U; bit < 8U; ++bit)
  {
    result = static_cast<std::uint8_t>((result << 1) | ((value >> bit) & 0x01U));
  }
  return result;
}

// Brightness and drive-current fields travel most-significant-bit first, hence
// the reversal; bit0 selects the 6 grids x 7 segments mode.
constexpr std::uint8_t kDisplayModeValue = static_cast<std::uint8_t>(
    reverseBits(static_cast<std::uint8_t>((kDisplayIntensity << 1) | 0x01U)) |
    (reverseBits(static_cast<std::uint8_t>((kDisplayDriveCurrent + 1U) & 0x07U)) >> 4) |
    0x01U);

// Segment patterns for digit codes 0x00..0x0F, plus 0x10 for blank.
constexpr std::uint8_t kSegmentPatterns[] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, // 0-7
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, // 8, 9, A-F
    0x00,                                           // 0x10: blank
};

/**
  * @brief  TM1652 transport: pushes digit codes as segment bytes.
  * @note   Uses the hardware UART (USART1 TX on the CH32V003), so that pin is
  *         not available for a second serial port.
  */
class Tm1652Display
{
public:
  void begin()
  {
    Serial.begin(kDisplayBaud, SERIAL_8E1);

    const std::uint8_t clear[rb::reg::kDisplayDigits] = {};
    writeFrame(clear);
    sendCommand(kDisplayModeValue);
  }

  void writeDigits(const std::uint8_t digits[rb::reg::kDisplayDigits])
  {
    std::uint8_t segments[rb::reg::kDisplayDigits] = {};

    for (std::uint8_t i = 0U; i < rb::reg::kDisplayDigits; ++i)
    {
      segments[i] = segmentPattern(digits[i]);
    }
    writeFrame(segments);
  }

private:
  static std::uint8_t segmentPattern(std::uint8_t digit)
  {
    return digit <= rb::reg::kDigitBlank ? kSegmentPatterns[digit] : 0x00U;
  }

  void writeFrame(const std::uint8_t segments[rb::reg::kDisplayDigits])
  {
    waitCommandGap();
    Serial.write(kDisplayCommandAddress);

    for (std::uint8_t i = 0U; i < rb::reg::kDisplayDigits; ++i)
    {
      Serial.write(segments[i]);
    }
    lastCommandUs_ = micros();
  }

  void sendCommand(std::uint8_t value)
  {
    waitCommandGap();
    Serial.write(kDisplayCommandMode);
    Serial.write(value);
    lastCommandUs_ = micros();
  }

  void waitCommandGap() const
  {
    while (static_cast<std::uint32_t>(micros() - lastCommandUs_) < kDisplayCommandGapUs)
    {
    }
  }

  std::uint32_t lastCommandUs_ = 0U;
};

/**
  * @brief  Hardware hooks consumed by the Peripheral engine.
  */
class ArduinoPeripheralIo final : public rb::PeripheralIo
{
public:
  std::uint8_t readButtons() override
  {
    std::uint8_t mask = 0U;

    if (digitalRead(PIN_BUTTON1) == LOW) mask |= rb::reg::kButton1;
    if (digitalRead(PIN_BUTTON2) == LOW) mask |= rb::reg::kButton2;
    if (digitalRead(PIN_BUTTON3) == LOW) mask |= rb::reg::kButton3;
    if (digitalRead(PIN_BUTTON4) == LOW) mask |= rb::reg::kButton4;

    return mask;
  }

  bool batteryConnected() override
  {
    return digitalRead(PIN_BATTERY_PRESENT) == LOW; // adjust polarity
  }

  std::uint16_t batteryMillivolts() override
  {
    const std::uint32_t counts = static_cast<std::uint32_t>(analogRead(PIN_BATTERY_ADC));
    return static_cast<std::uint16_t>(
        (counts * kBatteryAdcRefMillivolts * kBatteryDividerRatio) / kBatteryAdcFullScale);
  }

  std::uint16_t pressureAdc() override
  {
    return static_cast<std::uint16_t>(analogRead(PIN_PRESSURE_ADC));
  }

  std::uint32_t pressurizedTimeMs() override
  {
    return pressurizedMs_;
  }

  void displayWrite(const std::uint8_t digits[rb::reg::kDisplayDigits]) override
  {
    for (std::uint8_t i = 0U; i < rb::reg::kDisplayDigits; ++i)
    {
      displayDigits_[i] = digits[i];
    }
    displayDirty_ = true;
  }

  void applyPumpDuty(std::uint16_t duty) override
  {
    // CH32V003 analogWrite() takes 0..255; duty is 0..1000 (0.1 % steps).
    const std::uint16_t pwm = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(duty) * 255U + 500U) / rb::reg::kPumpDutyMax);
    analogWrite(PIN_PUMP, pwm);
  }

  void applyValveState(std::uint8_t state) override
  {
    digitalWrite(PIN_VALVE, state == rb::reg::kValveOpen ? HIGH : LOW);
  }

  /** Accumulate the time the system spends pressurized. */
  void updatePressure()
  {
    const std::uint32_t now = millis();
    const std::uint32_t elapsed = now - lastPressureSampleMs_;
    lastPressureSampleMs_ = now;

    if (pressureAdc() >= kPressureThreshold)
    {
      pressurizedMs_ += elapsed;
    }
    else
    {
      pressurizedMs_ = 0U;
    }
  }

  void beginDisplay()
  {
    display_.begin();
  }

  /** True when the digit registers changed since the last flush. */
  bool displayPending() const
  {
    return displayDirty_;
  }

  /** Push the digit registers to the TM1652 (blocks for a few milliseconds). */
  void flushDisplay()
  {
    display_.writeDigits(displayDigits_);
    displayDirty_ = false;
  }

private:
  std::uint8_t displayDigits_[rb::reg::kDisplayDigits]{};
  std::uint32_t pressurizedMs_ = 0U;
  std::uint32_t lastPressureSampleMs_ = 0U;
  Tm1652Display display_;
  bool displayDirty_ = false;
};

ArduinoPeripheralIo io;
rb::Peripheral peripheral(io);

// Time of the last CLK edge, used to abort partial frames and to schedule the
// blocking display refresh while no frame is in flight.
std::uint32_t lastBusEdgeMs = 0U;

/**
  * @brief  Feed one CLK edge to the engine.
  * @note   On the falling edge the peripheral sets up (or releases) DATA; on the
  *         rising edge it samples DATA and advances the state machine.
  */
void pollBus()
{
  static bool lastClock = LOW;

  const bool clock = digitalRead(PIN_CLK) == HIGH;

  if (clock == lastClock)
  {
    // Abort a partial frame if the controller stops clocking.
    if (!peripheral.isIdle() && ((millis() - lastBusEdgeMs) > kBusIdleTimeoutMs))
    {
      peripheral.reset();
      pinMode(PIN_DATA, INPUT_PULLUP);
    }
    return;
  }
  lastClock = clock;
  lastBusEdgeMs = millis();

  if (!clock)
  {
    if (peripheral.drivesData())
    {
      pinMode(PIN_DATA, OUTPUT);
      digitalWrite(PIN_DATA, peripheral.outputBit() ? HIGH : LOW);
    }
    else
    {
      pinMode(PIN_DATA, INPUT_PULLUP);
    }
  }
  else
  {
    peripheral.onBit(digitalRead(PIN_DATA) == HIGH);

    if (!peripheral.drivesData())
    {
      pinMode(PIN_DATA, INPUT_PULLUP); // release after a read / idle
    }
  }
}

void setup()
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

  io.beginDisplay();

  io.updatePressure(); // seed the millis() baseline
}

void loop()
{
  io.updatePressure();
  pollBus();

  // Send the pending digits once the link is idle, so the UART burst cannot
  // swallow a frame. The controller must leave a gap longer than
  // kDisplayQuietMs between polls for the display to update.
  if (io.displayPending() && peripheral.isIdle() &&
      ((millis() - lastBusEdgeMs) > kDisplayQuietMs))
  {
    io.flushDisplay();
  }
}
