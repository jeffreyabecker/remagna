/**
  ******************************************************************************
  * @file    protocol.h
  * @brief   Wire definition, register map and both link engines.
  *
  * Contents:
  *   - reg:      register addresses and value constants
  *   - protocol: frame constants and button helpers
  *   - Controller / ValveState: the controller-side driver
  *   - Peripheral: the peripheral-side device model
  *
  * Frame (controller-driven, LSB-first):
  *
  *   START     1 bit, DATA driven low
  *   COMMAND   8 bits, bit0 = R/W (0 = write, 1 = read), bits 1..7 reserved
  *   ADDRESS   8 bits, first register offset
  *   LENGTH    8 bits, number of data bytes (1..255)
  *   write:    DATA, controller drives
  *   read:     TURN (controller releases DATA), then DATA driven by peripheral
  *
  * Registers auto-increment across LENGTH bytes. CLK idles low, DATA idles high.
  * The controller releases DATA at the end of every transaction.
  *
  * Register map. Multi-byte values are little-endian and auto-increment across a
  * transfer.
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

#ifndef REMAGNA_BITBANG_PROTOCOL_H
#define REMAGNA_BITBANG_PROTOCOL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace remagna::bitbang::reg
{

inline constexpr std::uint8_t DeviceId = 0x00U;
inline constexpr std::uint8_t MapVersion = 0x01U;
inline constexpr std::uint8_t Display = 0x02U;
inline constexpr std::uint8_t Buttons = 0x08U;
inline constexpr std::uint8_t BatteryStatus = 0x09U;
inline constexpr std::uint8_t BatteryMillivolts = 0x0AU;
inline constexpr std::uint8_t PressureAdc = 0x0CU;
inline constexpr std::uint8_t PumpDuty = 0x0EU;
inline constexpr std::uint8_t ValveState = 0x10U;
inline constexpr std::uint8_t PressurizedTimeMs = 0x11U;

inline constexpr std::uint8_t MapSize = 0x15U;

inline constexpr std::uint8_t DeviceIdValue = 0x4DU; // 'M'
inline constexpr std::uint8_t MapVersionValue = 0x01U;

inline constexpr std::uint8_t DisplayDigits = 6U;
inline constexpr std::uint8_t DigitBlank = 0x10U;

inline constexpr std::uint8_t Button1 = 0x01U;
inline constexpr std::uint8_t Button2 = 0x02U;
inline constexpr std::uint8_t Button3 = 0x04U;
inline constexpr std::uint8_t Button4 = 0x08U;

inline constexpr std::uint8_t BatteryConnected = 0x01U;

inline constexpr std::uint16_t PumpDutyMax = 1000U;

inline constexpr std::uint8_t ValveClosed = 0x00U;
inline constexpr std::uint8_t ValveOpen = 0x01U;

} // namespace remagna::bitbang::reg

namespace remagna::bitbang::protocol
{

inline constexpr std::uint8_t BitsPerByte = 8U;

inline constexpr bool StartBit = false;

inline constexpr std::uint8_t CommandWrite = 0x00U;
inline constexpr std::uint8_t CommandRead = 0x01U;
inline constexpr std::uint8_t CommandReadMask = 0x01U;

/**
  * @brief  True when button 1..4 is pressed in a Buttons register value.
  * @param  state Value read from (or written to) the Buttons register.
  * @param  index Button number, 1..4.
  */
inline constexpr bool pressed(std::uint8_t state, std::uint8_t index) noexcept
{
  return (index >= 1U) && (index <= 4U) &&
         ((state & static_cast<std::uint8_t>(0x01U << (index - 1U))) != 0U);
}

/**
  * @brief  Assemble a Buttons register value from individual button levels.
  */
inline constexpr std::uint8_t build(bool button1, bool button2, bool button3, bool button4) noexcept
{
  return static_cast<std::uint8_t>((button1 ? reg::Button1 : 0U) |
                                   (button2 ? reg::Button2 : 0U) |
                                   (button3 ? reg::Button3 : 0U) |
                                   (button4 ? reg::Button4 : 0U));
}

} // namespace remagna::bitbang::protocol

namespace remagna::bitbang
{

enum class ValveState : std::uint8_t
{
  Closed = reg::ValveClosed,
  Open = reg::ValveOpen,
};

/**
  * @brief  Controller API for the bit-banged peripheral.
  *
  * The platform owns the two GPIOs (CLOCK and DATA), the pin directions and all
  * timing; the driver only asks it to clock one bit at a time.
  *
  * Reads return the value the peripheral supplied. All transfers are
  * host-clocked and LSB-first, reads release the data line for one turnaround
  * bit first, and calls are blocking and not thread-safe.
  */
class Controller
{
public:
  static constexpr std::size_t DisplayDigits = reg::DisplayDigits;
  static constexpr std::uint16_t PumpDutyMax = reg::PumpDutyMax;

  Controller() = default;
  virtual ~Controller() = default;

  Controller(const Controller &) = delete;
  Controller &operator=(const Controller &) = delete;

  std::uint8_t deviceId();
  std::uint8_t mapVersion();

  /** Digit values: 0x00..0x0F show 0..F, reg::DigitBlank blanks a digit. */
  std::array<std::uint8_t, DisplayDigits> display();
  void writeDisplay(const std::array<std::uint8_t, DisplayDigits> &digits);

  /** bit0..3 = button 1..4, 1 = pressed. */
  std::uint8_t buttons();

  bool batteryConnected();
  std::uint16_t batteryMillivolts();
  std::uint16_t pressureAdc();

  /** Duty in tenths of a percent: 0..1000 = 0.0..100.0 %. */
  std::uint16_t pumpDuty();
  /** @retval false when tenthsPercent exceeds PumpDutyMax; nothing is sent. */
  bool writePumpDuty(std::uint16_t tenthsPercent);

  /** @retval nullopt when the register holds neither Closed nor Open. */
  std::optional<ValveState> valveState();
  void writeValveState(ValveState state);

  std::uint32_t pressurizedTimeMs();

protected:
  /**
    * @brief  Clock exactly one bit, implemented by each platform.
    *
    * Semantics for each call:
    *  - drive == true : drive txLevel on DATA, pulse CLOCK, return the DATA level
    *                    sampled around the clock edge (the device is not expected
    *                    to drive while the host drives).
    *  - drive == false: release DATA (input with pull-up), pulse CLOCK, return the
    *                    DATA level driven by the device.
    *
    * The clock idles low and bits are transferred LSB-first. The platform decides
    * the edge order and delay; the driver never touches GPIOs directly.
    */
  virtual bool clockBit(bool drive, bool txLevel) = 0;

  /**
    * @brief  Return DATA to input/pull-up when the bus is idle.
    *
    * Called once at the end of every read/write transaction. The peripheral
    * uses the idle-high level to detect the next START bit.
    */
  virtual void releaseData() = 0;

private:
  void writeByte(std::uint8_t value);
  std::uint8_t readByte();

  void readRegister(std::uint8_t address, std::uint8_t *data, std::uint8_t length);
  void writeRegister(std::uint8_t address, const std::uint8_t *data, std::uint8_t length);
};

/**
  * @brief  Peripheral-side device model for the bit-banged link.
  *
  * The class owns the register file and the frame state machine: the sketch
  * writes the input registers with the setters, the controller reads them and
  * writes the output registers, and the sketch polls takeUpdates() and reads
  * the outputs with the getters. Nothing is sampled or applied inside the
  * protocol engine.
  */
class Peripheral
{
public:
  static constexpr std::size_t DisplayDigits = reg::DisplayDigits;
  static constexpr std::uint16_t PumpDutyMax = reg::PumpDutyMax;

  /** What the sketch should do with DATA after a clock edge. */
  enum class Drive : std::uint8_t
  {
    Hold,    // leave DATA alone (keeps a read bit valid while CLK is high)
    Release, // input with pull-up
    Low,     // drive low
    High,    // drive high
  };

  /** Output registers written by the controller since the last takeUpdates(). */
  enum Update : std::uint8_t
  {
    UpdateNone = 0U,
    UpdateDisplay = 1U << 0,
    UpdatePumpDuty = 1U << 1,
    UpdateValveState = 1U << 2,
  };

  Peripheral() noexcept;

  Peripheral(const Peripheral &) = delete;
  Peripheral &operator=(const Peripheral &) = delete;

  /**
    * @brief  Advance the engine by one CLK edge; call this from the CLK ISR.
    *
    * @param  clockHigh  CLK level after the edge.
    * @param  dataLevel  DATA level sampled by the sketch at the edge.
    * @retval Drive      Action to apply to DATA now; Hold leaves the pin alone.
    *
    * The falling edge sets up (or releases) DATA for the upcoming bit and the
    * rising edge consumes the sampled bit, so DATA keeps its drive state while
    * CLK is high and the controller can sample it late in the phase.
    *
    * ISR-safe: bounded work, no callbacks and no platform access.
    */
  Drive onBitClocked(bool clockHigh, bool dataLevel);

  /** True when the engine is waiting for a START bit. */
  bool isIdle() const;

  /** Drop a partial frame; call from the main loop once the bus is quiet. */
  void reset();

  /** Return the pending update flags and clear only the bits reported. */
  std::uint8_t takeUpdates();

  // --- Inputs: values the controller reads ---------------------------------

  /** bit0..3 = button 1..4, 1 = pressed. */
  void setButtons(std::uint8_t mask);
  void setBatteryConnected(bool connected);
  void setBatteryMillivolts(std::uint16_t millivolts);
  void setPressureAdc(std::uint16_t counts);
  void setPressurizedTimeMs(std::uint32_t milliseconds);

  // --- Outputs: values the controller wrote --------------------------------

  /** Digit values: 0x00..0x0F show 0..F, reg::DigitBlank blanks a digit. */
  std::array<std::uint8_t, DisplayDigits> display() const;

  /** Duty in tenths of a percent, clamped to PumpDutyMax. */
  std::uint16_t pumpDuty() const;

  bool valveOpen() const;

private:
  enum class Phase : std::uint8_t
  {
    Idle,
    Command,
    Address,
    Length,
    WriteData,
    Turnaround,
    ReadData,
  };

  void initializeRegisters();
  void onBit(bool dataBit);
  void finishWrite();
  Drive dataDrive() const;

  std::uint8_t readRegister(std::uint8_t address) const;
  void writeRegister(std::uint8_t address, std::uint8_t value);
  std::uint16_t readRegister16(std::uint8_t address) const;
  void writeRegister16(std::uint8_t address, std::uint16_t value);
  void writeRegister32(std::uint8_t address, std::uint32_t value);

  volatile std::uint8_t registers_[reg::MapSize];
  volatile std::uint8_t updates_;
  volatile Phase phase_;
  std::uint8_t bitIndex_;
  std::uint8_t command_;
  std::uint8_t address_;
  std::uint8_t length_;
  std::uint8_t remaining_;
  std::uint8_t dataByte_;
  std::uint8_t readByte_;
  std::uint8_t writeStart_;
  std::uint8_t writeLength_;
  bool reading_;
};

} // namespace remagna::bitbang

#endif // REMAGNA_BITBANG_PROTOCOL_H
