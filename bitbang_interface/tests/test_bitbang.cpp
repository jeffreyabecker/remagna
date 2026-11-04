/**
  ******************************************************************************
  * @file    test_bitbang.cpp
  * @brief   Loopback tests: real Controller against the real Peripheral engine.
  *
  * LoopbackController joins the two engines bit by bit, so every feature test
  * exercises the shared protocol implementation on both ends. The controller
  * reads the values the test pushed into the peripheral with its setters, and
  * the values the controller writes come back through the getters and update
  * flags. sendRawWrite() plays a rogue controller that skips the API checks.
  ******************************************************************************
  */

#include <cstdint>
#include <iostream>

#include "protocol.h"

namespace
{

int failures = 0;

bool sameDigits(const char *actual, const char *expected)
{
  for (std::uint8_t i = 0U; i < DisplayDigits; ++i)
  {
    if (actual[i] != expected[i])
    {
      return false;
    }
  }
  return true;
}

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

static_assert(build(false, false, false, false) == 0x00U);
static_assert(build(true, false, true, false) == 0x05U);
static_assert(build(true, true, true, true) == 0x0FU);
static_assert(pressed(0x05U, 1U));
static_assert(!pressed(0x05U, 2U));
static_assert(pressed(0x05U, 3U));
static_assert(!pressed(0x05U, 0U));
static_assert(!pressed(0x05U, 5U));

class LoopbackController final : public Controller
{
public:
  explicit LoopbackController(Peripheral &peripheral) : peripheral_(peripheral)
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

    // Falling edge: the peripheral decides what it puts on DATA.
    const Peripheral::Drive setup = peripheral_.onBitClocked(false, false);
    const bool deviceBit = setup == Peripheral::Drive::High;

    // Rising edge: the peripheral samples whichever level is on the wire.
    (void)peripheral_.onBitClocked(true, drive ? txLevel : deviceBit);
    return deviceBit;
  }

  void releaseData() override
  {
    ++idleReleases;
    peripheral_.reset();
  }

private:
  Peripheral &peripheral_;
};

namespace
{

/** Drive a write frame straight into the peripheral, bypassing the API checks. */
void sendRawWrite(Peripheral &peripheral,
                  std::uint8_t address,
                  const std::uint8_t *data,
                  std::uint8_t length)
{
  const auto bit = [&peripheral](bool level)
  {
    (void)peripheral.onBitClocked(true, level);
  };

  bit(false); // START

  for (std::uint8_t i = 0U; i < 8U; ++i) bit(false); // command 0x00: write

  for (std::uint8_t i = 0U; i < 8U; ++i) bit(((address >> i) & 0x01U) != 0U);
  for (std::uint8_t i = 0U; i < 8U; ++i) bit(((length >> i) & 0x01U) != 0U);

  for (std::uint8_t byte = 0U; byte < length; ++byte)
  {
    for (std::uint8_t i = 0U; i < 8U; ++i) bit(((data[byte] >> i) & 0x01U) != 0U);
  }
}

void testIdentity(Controller &controller)
{
  CHECK_EQ(controller.deviceId(), DeviceIdValue);
  CHECK_EQ(controller.mapVersion(), MapVersionValue);
}

void testDisplay(Controller &controller, Peripheral &peripheral)
{
  char blank[DisplayDigits];
  controller.display(blank);

  for (const char digit : blank)
  {
    CHECK_EQ(digit, static_cast<char>(DigitBlank));
  }

  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdateNone));

  const char expected[DisplayDigits] = {0, 1, 2, 3, 4, 5};
  controller.writeDisplay(expected);
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdateDisplay));

  char fromPeripheral[DisplayDigits];
  peripheral.display(fromPeripheral);
  CHECK(sameDigits(fromPeripheral, expected));

  char readBack[DisplayDigits];
  controller.display(readBack);
  CHECK(sameDigits(readBack, expected));
}

void testInputs(Controller &controller, Peripheral &peripheral)
{
  peripheral.setButtons(build(true, false, true, false));

  const std::uint8_t mask = controller.buttons();
  CHECK(pressed(mask, 1U));
  CHECK(!pressed(mask, 2U));
  CHECK(pressed(mask, 3U));
  CHECK(!pressed(mask, 4U));

  peripheral.setBatteryConnected(true);
  CHECK(controller.batteryConnected());

  peripheral.setBatteryConnected(false);
  CHECK(!controller.batteryConnected());

  peripheral.setBatteryMillivolts(3000U);
  CHECK_EQ(controller.batteryMillivolts(), 3000);

  peripheral.setPressureAdc(0x0123U);
  CHECK_EQ(controller.pressureAdc(), 0x0123);

  peripheral.setPressurizedTimeMs(123456U);
  CHECK_EQ(controller.pressurizedTimeMs(), 123456U);
}

void testPump(Controller &controller, Peripheral &peripheral)
{
  CHECK_EQ(controller.pumpDuty(), 0);

  CHECK(controller.writePumpDuty(750U));
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdatePumpDuty));
  CHECK_EQ(peripheral.pumpDuty(), 750);
  CHECK_EQ(controller.pumpDuty(), 750);

  CHECK(controller.writePumpDuty(PumpDutyMax));
  CHECK_EQ(peripheral.pumpDuty(), PumpDutyMax);
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdatePumpDuty));

  CHECK(!controller.writePumpDuty(static_cast<std::uint16_t>(PumpDutyMax + 1U)));
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdateNone));
  CHECK_EQ(peripheral.pumpDuty(), PumpDutyMax);

  // A raw over-limit write is clamped by the peripheral.
  const std::uint8_t over[2] = {0xFFU, 0x03U}; // 1023
  sendRawWrite(peripheral, PumpDuty, over, 2U);
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdatePumpDuty));
  CHECK_EQ(peripheral.pumpDuty(), PumpDutyMax);
  CHECK_EQ(controller.pumpDuty(), PumpDutyMax);
}

void testValve(Controller &controller, Peripheral &peripheral)
{
  CHECK(!peripheral.valveOpen());
  CHECK(controller.valveState() == ValveState::Closed);

  controller.writeValveState(ValveState::Open);
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdateValveState));
  CHECK(peripheral.valveOpen());
  CHECK(controller.valveState() == ValveState::Open);

  controller.writeValveState(ValveState::Closed);
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdateValveState));
  CHECK(!peripheral.valveOpen());
  CHECK(controller.valveState() == ValveState::Closed);

  // An out-of-spec register value is not reported as a valve state.
  const std::uint8_t unknown = 7U;
  sendRawWrite(peripheral, ValveStateReg, &unknown, 1U);
  CHECK_EQ(peripheral.takeUpdates(), static_cast<std::uint8_t>(Peripheral::UpdateValveState));
  CHECK(!peripheral.valveOpen());
  CHECK(!controller.valveState().has_value());
}

void testFrameShape(LoopbackController &controller)
{
  const std::uint32_t releasedBefore = controller.releasedBits;
  const std::uint32_t idleBefore = controller.idleReleases;

  (void)controller.pressurizedTimeMs();
  CHECK_EQ(controller.releasedBits - releasedBefore, 1U + (4U * 8U));
  CHECK_EQ(controller.idleReleases - idleBefore, 1U);

  const std::uint32_t releasedBeforeWrite = controller.releasedBits;
  CHECK(controller.writePumpDuty(750U));
  CHECK_EQ(controller.releasedBits, releasedBeforeWrite);
  CHECK_EQ(controller.idleReleases - idleBefore, 2U);
}

void testFinalReadBitHeld(Peripheral &peripheral)
{
  peripheral.reset();

  const auto bit = [&peripheral](bool level)
  {
    (void)peripheral.onBitClocked(true, level);
  };

  bit(false); // START, controller drives 0

  for (int i = 0; i < 8; ++i) bit(i == 0); // command 0x01: read
  for (int i = 0; i < 8; ++i) bit(i == 0); // address 0x01: map version
  for (int i = 0; i < 8; ++i) bit(i == 0); // length 1

  CHECK(peripheral.onBitClocked(false, false) == Peripheral::Drive::Release); // turnaround
  (void)peripheral.onBitClocked(true, true);

  for (int i = 0; i < 8; ++i)
  {
    const Peripheral::Drive drive = peripheral.onBitClocked(false, false);
    CHECK(drive != Peripheral::Drive::Release); // the last bit is driven too
    (void)peripheral.onBitClocked(true, drive == Peripheral::Drive::High);
  }

  CHECK(peripheral.isIdle());
}

void testAbortRecovery(Peripheral &peripheral, Controller &controller)
{
  peripheral.reset();
  (void)peripheral.onBitClocked(true, false); // START
  (void)peripheral.onBitClocked(true, false); // command bit 0: write
  (void)peripheral.onBitClocked(true, true);  // first address bit
  CHECK(!peripheral.isIdle());

  peripheral.reset();
  CHECK(peripheral.isIdle());
  CHECK_EQ(controller.mapVersion(), MapVersionValue);
}

} // namespace

int main()
{
  Peripheral peripheral;
  LoopbackController controller(peripheral);

  testIdentity(controller);
  testDisplay(controller, peripheral);
  testInputs(controller, peripheral);
  testPump(controller, peripheral);
  testValve(controller, peripheral);
  testFrameShape(controller);
  testFinalReadBitHeld(peripheral);
  testAbortRecovery(peripheral, controller);

  if (failures != 0)
  {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "bitbang loopback tests passed\n";
  return 0;
}
