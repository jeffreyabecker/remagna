/**
  ******************************************************************************
  * @file    test_bitbang.cpp
  * @brief   Loopback tests: real Controller against the real Peripheral engine.
  *
  * MockPeripheralIo provides canned inputs and records hardware outputs.
  * LoopbackController joins the two engines bit by bit, so every feature test
  * exercises the shared protocol implementation on both ends.
  ******************************************************************************
  */

#include <array>
#include <cstdint>
#include <iostream>

#include "controller.h"
#include "peripheral.h"

namespace rb = remagna::bitbang;

namespace
{

int failures = 0;

void check(bool condition, const char *expression, const char *file, int line)
{
  if (!condition)
  {
    std::cerr << file << ':' << line << ": CHECK failed: " << expression << '\n';
    ++failures;
  }
}

} // namespace

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) check((actual) == (expected), #actual " == " #expected, __FILE__, __LINE__)

class MockPeripheralIo final : public rb::PeripheralIo
{
public:
  std::uint8_t buttons = 0U;
  bool battery = false;
  std::uint16_t batteryMv = 0U;
  std::uint16_t pressure = 0U;
  std::uint32_t pressurizedMs = 0U;

  std::array<std::uint8_t, rb::reg::kDisplayDigits> display{};
  std::uint16_t pumpDuty = 0U;
  std::uint8_t valve = 0U;
  std::uint32_t displayWrites = 0U;
  std::uint32_t pumpWrites = 0U;
  std::uint32_t valveWrites = 0U;

  std::uint8_t readButtons() override
  {
    return buttons;
  }

  bool batteryConnected() override
  {
    return battery;
  }

  std::uint16_t batteryMillivolts() override
  {
    return batteryMv;
  }

  std::uint16_t pressureAdc() override
  {
    return pressure;
  }

  std::uint32_t pressurizedTimeMs() override
  {
    return pressurizedMs;
  }

  void displayWrite(const std::uint8_t digits[rb::reg::kDisplayDigits]) override
  {
    for (std::uint8_t i = 0U; i < rb::reg::kDisplayDigits; ++i)
    {
      display[i] = digits[i];
    }
    ++displayWrites;
  }

  void applyPumpDuty(std::uint16_t duty) override
  {
    pumpDuty = duty;
    ++pumpWrites;
  }

  void applyValveState(std::uint8_t state) override
  {
    valve = state;
    ++valveWrites;
  }
};

class LoopbackController final : public rb::Controller
{
public:
  explicit LoopbackController(rb::Peripheral &peripheral) : peripheral_(peripheral)
  {
  }

  std::uint32_t releasedBits = 0U;
  std::uint32_t idleReleases = 0U;

protected:
  bool clockBit(bool drive, bool txLevel) override
  {
    if (!drive)
    {
      ++releasedBits;
    }

    const bool deviceBit = peripheral_.outputBit();
    peripheral_.onBit(drive ? txLevel : deviceBit);
    return deviceBit;
  }

  void releaseData() override
  {
    ++idleReleases;
    peripheral_.reset();
  }

private:
  rb::Peripheral &peripheral_;
};

namespace
{

void testIdentity(rb::Controller &controller)
{
  std::uint8_t id = 0U;
  CHECK_EQ(controller.readDeviceId(id), rb::Status::Ok);
  CHECK_EQ(id, rb::reg::kDeviceIdValue);

  std::uint8_t version = 0U;
  CHECK_EQ(controller.readMapVersion(version), rb::Status::Ok);
  CHECK_EQ(version, rb::reg::kMapVersionValue);
}

void testDisplay(rb::Controller &controller, MockPeripheralIo &io)
{
  std::array<std::uint8_t, rb::Controller::kDisplayDigits> digits{};
  CHECK_EQ(controller.readDisplay(digits), rb::Status::Ok);

  for (const auto digit : digits)
  {
    CHECK_EQ(digit, rb::reg::kDigitBlank);
  }

  const std::array<std::uint8_t, rb::Controller::kDisplayDigits> expected{0U, 1U, 2U, 3U, 4U, 5U};
  CHECK_EQ(controller.writeDisplay(expected), rb::Status::Ok);
  CHECK(io.display == expected);
  CHECK_EQ(io.displayWrites, 1U);

  digits.fill(0U);
  CHECK_EQ(controller.readDisplay(digits), rb::Status::Ok);
  CHECK(digits == expected);
}

void testInputs(rb::Controller &controller, MockPeripheralIo &io)
{
  std::uint8_t buttons = 0U;
  CHECK_EQ(controller.readButtons(buttons), rb::Status::Ok);
  CHECK_EQ(buttons, static_cast<std::uint8_t>(rb::reg::kButton1 | rb::reg::kButton3));

  bool connected = false;
  CHECK_EQ(controller.readBatteryConnected(connected), rb::Status::Ok);
  CHECK(connected);

  io.battery = false;
  CHECK_EQ(controller.readBatteryConnected(connected), rb::Status::Ok);
  CHECK(!connected);
  io.battery = true;

  std::uint16_t millivolts = 0U;
  CHECK_EQ(controller.readBatteryMillivolts(millivolts), rb::Status::Ok);
  CHECK_EQ(millivolts, 3000);

  std::uint16_t counts = 0U;
  CHECK_EQ(controller.readPressureAdc(counts), rb::Status::Ok);
  CHECK_EQ(counts, 0x0123);

  std::uint32_t milliseconds = 0U;
  CHECK_EQ(controller.readPressurizedTimeMs(milliseconds), rb::Status::Ok);
  CHECK_EQ(milliseconds, 123456U);
}

void testPump(rb::Controller &controller, MockPeripheralIo &io)
{
  std::uint16_t duty = 0U;
  CHECK_EQ(controller.readPumpDuty(duty), rb::Status::Ok);
  CHECK_EQ(duty, 0);

  CHECK_EQ(controller.writePumpDuty(750U), rb::Status::Ok);
  CHECK_EQ(io.pumpDuty, 750);
  CHECK_EQ(io.pumpWrites, 1U);

  CHECK_EQ(controller.readPumpDuty(duty), rb::Status::Ok);
  CHECK_EQ(duty, 750);

  CHECK_EQ(controller.writePumpDuty(rb::Controller::kPumpDutyMax), rb::Status::Ok);
  CHECK_EQ(io.pumpDuty, rb::Controller::kPumpDutyMax);

  const std::uint32_t writesBefore = io.pumpWrites;
  CHECK_EQ(controller.writePumpDuty(static_cast<std::uint16_t>(rb::Controller::kPumpDutyMax + 1U)),
           rb::Status::InvalidArgument);
  CHECK_EQ(io.pumpWrites, writesBefore);

  // A raw over-limit write is clamped by the peripheral.
  const std::uint8_t over[2] = {0xFFU, 0x03U}; // 1023
  CHECK_EQ(controller.write(rb::reg::kPumpDuty, over, 2U), rb::Status::Ok);
  CHECK_EQ(io.pumpDuty, rb::Controller::kPumpDutyMax);
  CHECK_EQ(controller.readPumpDuty(duty), rb::Status::Ok);
  CHECK_EQ(duty, rb::Controller::kPumpDutyMax);
}

void testValve(rb::Controller &controller, MockPeripheralIo &io)
{
  rb::ValveState state = rb::ValveState::Closed;
  CHECK_EQ(controller.readValveState(state), rb::Status::Ok);
  CHECK(state == rb::ValveState::Closed);

  CHECK_EQ(controller.writeValveState(rb::ValveState::Open), rb::Status::Ok);
  CHECK_EQ(io.valve, rb::reg::kValveOpen);
  CHECK_EQ(io.valveWrites, 1U);

  CHECK_EQ(controller.readValveState(state), rb::Status::Ok);
  CHECK(state == rb::ValveState::Open);

  const std::uint8_t unknown = 7U;
  CHECK_EQ(controller.write(rb::reg::kValveState, &unknown, 1U), rb::Status::Ok);
  CHECK_EQ(controller.readValveState(state), rb::Status::InvalidArgument);
}

void testInvalidArguments(rb::Controller &controller)
{
  std::uint8_t byte = 0U;
  CHECK_EQ(controller.read(rb::reg::kDeviceId, nullptr, 1U), rb::Status::InvalidArgument);
  CHECK_EQ(controller.read(rb::reg::kDeviceId, &byte, 0U), rb::Status::InvalidArgument);
  CHECK_EQ(controller.write(rb::reg::kDeviceId, nullptr, 1U), rb::Status::InvalidArgument);
  CHECK_EQ(controller.write(rb::reg::kDeviceId, &byte, 0U), rb::Status::InvalidArgument);
}

void testFrameShape(LoopbackController &controller)
{
  const std::uint32_t releasedBefore = controller.releasedBits;
  const std::uint32_t idleBefore = controller.idleReleases;

  std::uint8_t raw[4] = {};
  CHECK_EQ(controller.read(rb::reg::kPressurizedTimeMs, raw, 4U), rb::Status::Ok);
  CHECK_EQ(controller.releasedBits - releasedBefore, 1U + (4U * 8U));
  CHECK_EQ(controller.idleReleases - idleBefore, 1U);

  const std::uint32_t releasedBeforeWrite = controller.releasedBits;
  const std::uint8_t payload[2] = {0x34U, 0x12U};
  CHECK_EQ(controller.write(rb::reg::kPumpDuty, payload, 2U), rb::Status::Ok);
  CHECK_EQ(controller.releasedBits, releasedBeforeWrite);
  CHECK_EQ(controller.idleReleases - idleBefore, 2U);
}

void testAbortRecovery(rb::Peripheral &peripheral, rb::Controller &controller)
{
  peripheral.reset();
  peripheral.onBit(false); // START
  peripheral.onBit(false); // command bit 0: write
  peripheral.onBit(true);  // first address bit
  CHECK(!peripheral.isIdle());

  peripheral.reset();
  CHECK(peripheral.isIdle());

  std::uint8_t version = 0U;
  CHECK_EQ(controller.read(rb::reg::kMapVersion, &version, 1U), rb::Status::Ok);
  CHECK_EQ(version, rb::reg::kMapVersionValue);
}

} // namespace

int main()
{
  MockPeripheralIo io;
  io.buttons = static_cast<std::uint8_t>(rb::reg::kButton1 | rb::reg::kButton3);
  io.battery = true;
  io.batteryMv = 3000U;
  io.pressure = 0x0123U;
  io.pressurizedMs = 123456U;

  rb::Peripheral peripheral(io);
  LoopbackController controller(peripheral);

  testIdentity(controller);
  testDisplay(controller, io);
  testInputs(controller, io);
  testPump(controller, io);
  testValve(controller, io);
  testInvalidArguments(controller);
  testFrameShape(controller);
  testAbortRecovery(peripheral, controller);

  if (failures != 0)
  {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "bitbang loopback tests passed\n";
  return 0;
}
