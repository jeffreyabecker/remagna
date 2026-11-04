/**
  ******************************************************************************
  * @file    Py32Peripheral.cpp
  * @brief   The magna peripheral on ZP008 rev 10: pins, CLK interrupt, mapping.
  *
  * The frame engine and the register file come from the bitbang_interface
  * protocol unit; this file only adds the platform side:
  *   - CLK is an input on both edges, DATA is released with the internal
  *     pull-up and driven push-pull for the read bits,
  *   - every clock edge is handed to the engine from the interrupt, and the
  *     DATA state it returns is applied immediately,
  *   - the main loop publishes the board inputs and applies the registers the
  *     controller wrote.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Py32Peripheral.h"

#include <cstdint>

#include "board.h"

namespace
{
/* Abort a partial frame when the controller stops clocking for this long. */
constexpr std::uint32_t BusIdleTimeoutMs = 10U;

/* Pressure ADC count that means "pressurised"; tune to the sensor. */
constexpr std::uint16_t PressureThreshold = 512U;

constexpr std::uint32_t ClkMask = BOARD_LINK_CLK_PIN;
constexpr std::uint32_t DataMask = BOARD_LINK_DATA_PIN;
constexpr std::uint32_t DataShift = 2U * BOARD_LINK_DATA_PIN_NUM;
constexpr std::uint32_t DataModeMask = 0x3U << DataShift;

/* PY32F002B implements EXTICR[0..1] only, so every EXTI capable pin is 0..7 and
 * the vector follows from the CLK pin number. */
constexpr IRQn_Type ClockIrq()
{
  return (BOARD_LINK_CLK_PIN_NUM <= 1U) ? EXTI0_1_IRQn
         : (BOARD_LINK_CLK_PIN_NUM <= 3U) ? EXTI2_3_IRQn
                                          : EXTI4_15_IRQn;
}

/** Keeps the CLK interrupt out of a multi-byte register access. */
class IrqGuard
{
public:
  IrqGuard() noexcept { __disable_irq(); }
  ~IrqGuard() noexcept { __enable_irq(); }

  IrqGuard(const IrqGuard &) = delete;
  IrqGuard &operator=(const IrqGuard &) = delete;
};

inline bool clockHigh()
{
  return (BOARD_LINK_CLK_PORT->IDR & ClkMask) != 0U;
}

inline bool dataLevel()
{
  return (BOARD_LINK_DATA_PORT->IDR & DataMask) != 0U;
}
} // namespace

void Py32Peripheral::init()
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* CLK: input, both edges interrupt. The controller drives it. */
  GPIO_InitTypeDef clk = {};
  clk.Pin = ClkMask;
  clk.Mode = GPIO_MODE_IT_RISING_FALLING;
  clk.Pull = GPIO_NOPULL;
  clk.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BOARD_LINK_CLK_PORT, &clk);

  /* DATA: idle released with the internal pull-up. */
  GPIO_InitTypeDef data = {};
  data.Pin = DataMask;
  data.Mode = GPIO_MODE_INPUT;
  data.Pull = GPIO_PULLUP;
  data.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BOARD_LINK_DATA_PORT, &data);

  /* Fixed for the lifetime of the bus, so the interrupt only touches
   * MODER and BSRR. */
  BOARD_LINK_DATA_PORT->OTYPER &= ~DataMask;
  BOARD_LINK_DATA_PORT->OSPEEDR |= (0x2U << DataShift);

  releaseData();
  appliedDrive_ = Drive::Release;
  reset();

  HAL_NVIC_SetPriority(ClockIrq(), 0U, 0U);
  HAL_NVIC_EnableIRQ(ClockIrq());
}

void Py32Peripheral::onClockEdge()
{
  /* Clear the flag first: an edge that arrives while this handler runs has to
   * stay pending so the core re-enters it. */
  EXTI->PR = ClkMask;

  const bool high = clockHigh();
  const bool level = dataLevel();

  lastEdgeMs_ = HAL_GetTick();

  const Drive target = onBitClocked(high, level);

  if ((target == Drive::Hold) || (target == appliedDrive_))
  {
    return; /* keep the current DATA state, e.g. mid read bit */
  }
  appliedDrive_ = target;

  switch (target)
  {
    case Drive::Release:
      releaseData();
      break;

    case Drive::High:
      driveData(true);
      break;

    case Drive::Low:
      driveData(false);
      break;

    default:
      break;
  }
}

void Py32Peripheral::poll()
{
  const std::uint32_t now = HAL_GetTick();

  if (!isIdle() && ((now - lastEdgeMs_) > BusIdleTimeoutMs))
  {
    const IrqGuard guard;
    reset();
    appliedDrive_ = Drive::Release;
    releaseData();
  }

  publishInputs();
  applyOutputs();
}

void Py32Peripheral::publishInputs()
{
  const std::array<bool, ButtonCount> buttonStates = readButtons();
  const bool battery = batteryConnected();
  const std::uint16_t millivolts = batteryMillivolts();
  const std::uint16_t pressure = pressureAdc();

  const std::uint32_t now = HAL_GetTick();
  if (pressure >= PressureThreshold)
  {
    pressurizedMs_ += now - pressureSampleMs_;
  }
  else
  {
    pressurizedMs_ = 0U;
  }
  pressureSampleMs_ = now;

  const IrqGuard guard;
  setButtons(build(buttonStates[0], buttonStates[1], buttonStates[2], buttonStates[3]));
  setBatteryConnected(battery);
  setBatteryMillivolts(millivolts);
  setPressureAdc(pressure);
  setPressurizedTimeMs(pressurizedMs_);
}

void Py32Peripheral::applyOutputs()
{
  std::uint8_t updates = UpdateNone;
  {
    const IrqGuard guard;
    updates = takeUpdates();
  }

  if ((updates & UpdateDisplay) != 0U)
  {
    std::array<std::uint8_t, DisplayDigits> digits = {};
    {
      const IrqGuard guard;
      digits = display();
    }
    displayWrite(digits);
  }

  if ((updates & UpdatePumpDuty) != 0U)
  {
    std::uint16_t duty = 0U;
    {
      const IrqGuard guard;
      duty = pumpDuty();
    }
    applyMotorDuty(duty);
  }

  if ((updates & UpdateValveState) != 0U)
  {
    bool open = false;
    {
      const IrqGuard guard;
      open = valveOpen();
    }
    applyValveState(open);
  }
}

void Py32Peripheral::releaseData()
{
  /* Input mode; PUPDR keeps the internal pull-up configured in init(). */
  BOARD_LINK_DATA_PORT->MODER &= ~DataModeMask;
}

void Py32Peripheral::driveData(bool high)
{
  /* ODR first, so the pin never glitches when it becomes an output. */
  BOARD_LINK_DATA_PORT->BSRR = high ? DataMask : (DataMask << 16U);
  BOARD_LINK_DATA_PORT->MODER =
      (BOARD_LINK_DATA_PORT->MODER & ~DataModeMask) | (0x1U << DataShift);
}

void Py32Peripheral::displayWrite(const std::array<std::uint8_t, DisplayDigits> &digits)
{
  (void)digits;
  /* TODO(ZP008 rev 10): convert the digits to segment patterns and refresh the
     display. */
}

std::array<bool, Py32Peripheral::ButtonCount> Py32Peripheral::readButtons()
{
  /* TODO(ZP008 rev 10): sample the four button inputs. */
  return {};
}

bool Py32Peripheral::batteryConnected()
{
  /* TODO(ZP008 rev 10): sample the battery-detect input. */
  return false;
}

std::uint16_t Py32Peripheral::batteryMillivolts()
{
  /* TODO(ZP008 rev 10): measure the battery divider with the ADC. */
  return 0U;
}

std::uint16_t Py32Peripheral::pressureAdc()
{
  /* TODO(ZP008 rev 10): start a conversion and return the raw ADC value. */
  return 0U;
}

void Py32Peripheral::applyMotorDuty(std::uint16_t tenthsPercent)
{
  /* PumpDutyMax (0..1000) maps onto the 0..255 PWM range. */
  const std::uint16_t pwm = static_cast<std::uint16_t>(
      (static_cast<std::uint32_t>(tenthsPercent) * 255U + PumpDutyMax / 2U) / PumpDutyMax);
  (void)pwm;
  /* TODO(ZP008 rev 10): write pwm to the motor PWM compare register. */
}

void Py32Peripheral::applyValveState(bool open)
{
  (void)open;
  /* TODO(ZP008 rev 10): drive the valve output. */
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
