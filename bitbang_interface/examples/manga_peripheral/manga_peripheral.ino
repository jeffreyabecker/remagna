/**
  ******************************************************************************
  * @file    manga_peripheral.ino
  * @brief   Arduino sketch: PERIPHERAL device for the manga machine.
  *
  * Target: CH32V003F4P6 (TSSOP20, 16 KB flash / 2 KB SRAM) running the
  * CH32V003 Arduino core. Adjust the pin defines to your board package.
  *
  * The protocol engine and register file come from the bitbang_interface
  * library (Peripheral). The sketch only:
  *   - runs onBitClocked() from the CLK interrupt and applies the DATA drive state
  *     it returns,
  *   - pushes fresh input values with the setters (buttons, battery, ADC,
  *     pressurized time),
  *   - consumes the takeUpdates() flags and reads the written values with the
  *     getters (display digits, pump duty, valve).
  *
  * Wiring:
  *   CLK   <- controller CLK
  *   DATA  <-> controller DATA (open-drain / pulled up)
  *   GND common
  *
  * The ISR does no hardware work beyond driving DATA, so the bus can run much
  * faster than a polled loop allows. Only the main loop touches the ADC, PWM
  * and the panel.
  ******************************************************************************
  */

#include <Arduino.h>
#include <array>
#include <protocol.h>

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
constexpr uint32_t BatteryAdcFullScale = 1023U; // 10-bit ADC
constexpr uint32_t BatteryAdcRefMillivolts = 3300U;
constexpr uint32_t BatteryDividerRatio = 11U;   // (R1 + R2) / R2

// Pressure ADC count that means "pressurized"; tune to the sensor.
constexpr std::uint16_t PressureThreshold = 512U;

// Abort a partial frame if the controller stops clocking for this long.
constexpr std::uint32_t BusIdleTimeoutUs = 10000U;

Peripheral peripheral;

// Written by the CLK ISR, read by the main loop.
volatile std::uint32_t lastEdgeUs = 0U;

// DATA state the ISR has applied, so unchanged levels skip the pin writes.
Peripheral::Drive appliedDrive = Peripheral::Drive::Release;

/**
  * @brief  CLK interrupt: hand the edge to the engine and drive DATA.
  * @note   onBitClocked() does no hardware access of its own, so this stays short
  *         enough for an ISR at any realistic bus rate.
  */
void onClockEdge()
{
  const bool clockHigh = digitalRead(PIN_CLK) == HIGH;
  const bool dataLevel = digitalRead(PIN_DATA) == HIGH;

  lastEdgeUs = micros();

  const Peripheral::Drive target = peripheral.onBitClocked(clockHigh, dataLevel);

  if ((target == Peripheral::Drive::Hold) || (target == appliedDrive))
  {
    return; // keep the current DATA state, e.g. mid read bit
  }
  appliedDrive = target;

  if (target == Peripheral::Drive::Release)
  {
    pinMode(PIN_DATA, INPUT_PULLUP);
    return;
  }

  pinMode(PIN_DATA, OUTPUT);
  digitalWrite(PIN_DATA, target == Peripheral::Drive::High ? HIGH : LOW);
}

std::uint32_t pressurizedMs = 0U;
std::uint32_t lastPressureSampleMs = 0U;

std::uint8_t readButtons()
{
  return build(digitalRead(PIN_BUTTON1) == LOW,
                             digitalRead(PIN_BUTTON2) == LOW,
                             digitalRead(PIN_BUTTON3) == LOW,
                             digitalRead(PIN_BUTTON4) == LOW);
}

std::uint16_t batteryMillivolts()
{
  const std::uint32_t counts = static_cast<std::uint32_t>(analogRead(PIN_BATTERY_ADC));
  return static_cast<std::uint16_t>(
      (counts * BatteryAdcRefMillivolts * BatteryDividerRatio) / BatteryAdcFullScale);
}

/** Accumulate the time the system spends pressurized. */
void updatePressure()
{
  const std::uint32_t now = millis();
  const std::uint32_t elapsed = now - lastPressureSampleMs;
  lastPressureSampleMs = now;

  if (analogRead(PIN_PRESSURE_ADC) >= PressureThreshold)
  {
    pressurizedMs += elapsed;
  }
  else
  {
    pressurizedMs = 0U;
  }
}

/** Map the 0..PumpDutyMax register value onto the 0..255 PWM range. */
void applyPumpDuty(std::uint16_t duty)
{
  const std::uint16_t pwm = static_cast<std::uint16_t>(
      (static_cast<std::uint32_t>(duty) * 255U + 500U) / PumpDutyMax);
  analogWrite(PIN_PUMP, pwm);
}

void publishInputs()
{
  peripheral.setButtons(readButtons());
  peripheral.setBatteryConnected(digitalRead(PIN_BATTERY_PRESENT) == LOW); // adjust polarity
  peripheral.setBatteryMillivolts(batteryMillivolts());
  peripheral.setPressureAdc(static_cast<std::uint16_t>(analogRead(PIN_PRESSURE_ADC)));
  peripheral.setPressurizedTimeMs(pressurizedMs);
}

/** Apply whatever the controller wrote since the last call. */
void applyOutputs()
{
  const std::uint8_t updates = peripheral.takeUpdates();

  if ((updates & Peripheral::UpdateDisplay) != 0U)
  {
    // No panel is wired up in this sketch yet: read the digits here.
    const std::array<std::uint8_t, DisplayDigits> digits = peripheral.display();
    (void)digits;
  }

  if ((updates & Peripheral::UpdatePumpDuty) != 0U)
  {
    applyPumpDuty(peripheral.pumpDuty());
  }

  if ((updates & Peripheral::UpdateValveState) != 0U)
  {
    digitalWrite(PIN_VALVE, peripheral.valveOpen() ? HIGH : LOW);
  }
}

/** Drop a partial frame when the controller stops clocking mid-transfer. */
void abortStalledFrame(std::uint32_t nowUs)
{
  if (!peripheral.isIdle() && ((nowUs - lastEdgeUs) > BusIdleTimeoutUs))
  {
    noInterrupts();
    peripheral.reset();
    interrupts();
    pinMode(PIN_DATA, INPUT_PULLUP);
    appliedDrive = Peripheral::Drive::Release;
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

  lastEdgeUs = micros();
  updatePressure(); // seed the millis() baseline
  publishInputs();

  attachInterrupt(digitalPinToInterrupt(PIN_CLK), onClockEdge, CHANGE);
}

void loop()
{
  abortStalledFrame(micros());

  updatePressure();
  publishInputs();
  applyOutputs();
}
