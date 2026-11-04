/**
  ******************************************************************************
  * @file    link.cpp
  * @brief   Peripheral side of the bit-banged link to the magna controller.
  *
  * This is the platform layer around the portable protocol engine in
  * bitbang_interface/src/protocol.cpp: it owns the CLK/DATA pins, feeds the
  * engine one clock edge per interrupt and drives DATA with the state the
  * engine asks for. Everything the controller reads or writes beyond that is
  * the register file inside Peripheral.
  *
  * Only the CLK EXTI handler touches the bus timing, so the link runs at
  * whatever rate the controller clocks it at; the main loop just publishes
  * inputs and consumes the update flags.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <array>
#include <cstddef>
#include <cstdint>

#include "main.h"
#include "link.h"
#include "protocol.h"

/* Exported types ------------------------------------------------------------*/
/* Compile-time check that the C constants in link.h match the engine's flags. */
static_assert(LINK_UPDATE_DISPLAY == static_cast<std::uint8_t>(Peripheral::UpdateDisplay),
              "LINK_UPDATE_DISPLAY is out of sync with Peripheral::Update");
static_assert(LINK_UPDATE_PUMP_DUTY == static_cast<std::uint8_t>(Peripheral::UpdatePumpDuty),
              "LINK_UPDATE_PUMP_DUTY is out of sync with Peripheral::Update");
static_assert(LINK_UPDATE_VALVE_STATE == static_cast<std::uint8_t>(Peripheral::UpdateValveState),
              "LINK_UPDATE_VALVE_STATE is out of sync with Peripheral::Update");
static_assert(LINK_DISPLAY_DIGITS == DisplayDigits, "LINK_DISPLAY_DIGITS mismatch");
static_assert(LINK_PUMP_DUTY_MAX == PumpDutyMax, "LINK_PUMP_DUTY_MAX mismatch");

/* Private define -----------------------------------------------------------*/
/* Abort a partial frame when the controller stops clocking for this long. */
namespace
{
constexpr std::uint32_t BusIdleTimeoutMs = 10U;

constexpr std::uint32_t ClkMask = BOARD_LINK_CLK_PIN;
constexpr std::uint32_t DataMask = BOARD_LINK_DATA_PIN;
constexpr std::uint32_t DataShift = 2U * BOARD_LINK_DATA_PIN_NUM;
constexpr std::uint32_t DataModeMask = 0x3U << DataShift;

/* PY32F002B has one EXTICR pair, so every EXTI capable pin is 0..7 and the
 * vector follows from the CLK pin number. */
constexpr IRQn_Type ClockIrq()
{
  return (BOARD_LINK_CLK_PIN_NUM <= 1U) ? EXTI0_1_IRQn
         : (BOARD_LINK_CLK_PIN_NUM <= 3U) ? EXTI2_3_IRQn
                                          : EXTI4_15_IRQn;
}

/* Private variables --------------------------------------------------------*/
Peripheral peripheral;

/* DATA state the ISR has applied, so unchanged levels skip the pin writes. */
Peripheral::Drive appliedDrive = Peripheral::Drive::Release;

/* Written by the CLK ISR, read by the main loop. */
volatile std::uint32_t lastEdgeMs = 0U;

/* Private functions --------------------------------------------------------*/
inline bool ClockHigh()
{
  return (BOARD_LINK_CLK_PORT->IDR & ClkMask) != 0U;
}

inline bool DataLevel()
{
  return (BOARD_LINK_DATA_PORT->IDR & DataMask) != 0U;
}

/** DATA as input; the pull-up configured in Link_Init stays active. */
inline void ReleaseData()
{
  BOARD_LINK_DATA_PORT->MODER &= ~DataModeMask;
}

/** DATA as push-pull output; ODR is written first so the pin never glitches. */
inline void DriveData(bool high)
{
  BOARD_LINK_DATA_PORT->BSRR = high ? DataMask : (DataMask << 16U);
  BOARD_LINK_DATA_PORT->MODER =
      (BOARD_LINK_DATA_PORT->MODER & ~DataModeMask) | (0x1U << DataShift);
}

/**
  * @brief  Keep the CLK interrupt out of a multi-byte register access.
  *
  * The engine updates its registers from the ISR, so a value spanning several
  * bytes must not be read or written halfway through a frame.
  */
class IrqGuard
{
public:
  IrqGuard() noexcept { __disable_irq(); }
  ~IrqGuard() noexcept { __enable_irq(); }

  IrqGuard(const IrqGuard &) = delete;
  IrqGuard &operator=(const IrqGuard &) = delete;
};
} // namespace

/* Exported functions -------------------------------------------------------*/
void Link_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* CLK: input, both edges interrupt. The controller drives it. */
  GPIO_InitTypeDef init = {0};
  init.Pin = ClkMask;
  init.Mode = GPIO_MODE_IT_RISING_FALLING;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BOARD_LINK_CLK_PORT, &init);

  /* DATA: idle released with the internal pull-up. */
  init.Pin = DataMask;
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BOARD_LINK_DATA_PORT, &init);

  /* Fixed for the lifetime of the bus, so the ISR only touches MODER/BSRR. */
  BOARD_LINK_DATA_PORT->OTYPER &= ~DataMask;
  BOARD_LINK_DATA_PORT->OSPEEDR |= (0x2U << DataShift);

  ReleaseData();
  appliedDrive = Peripheral::Drive::Release;
  peripheral.reset();

  HAL_NVIC_SetPriority(ClockIrq(), 0U, 0U);
  HAL_NVIC_EnableIRQ(ClockIrq());
}

void Link_OnClockEdge(void)
{
  /* Clear the flag first: an edge that arrives while this handler runs must
   * leave the line pending so the core re-enters it. */
  EXTI->PR = ClkMask;

  const bool clockHigh = ClockHigh();
  const bool dataLevel = DataLevel();

  lastEdgeMs = HAL_GetTick();

  const Peripheral::Drive target = peripheral.onBitClocked(clockHigh, dataLevel);

  if ((target == Peripheral::Drive::Hold) || (target == appliedDrive))
  {
    return; /* keep the current DATA state, e.g. mid read bit */
  }
  appliedDrive = target;

  switch (target)
  {
    case Peripheral::Drive::Release:
      ReleaseData();
      break;

    case Peripheral::Drive::High:
      DriveData(true);
      break;

    case Peripheral::Drive::Low:
      DriveData(false);
      break;

    default:
      break;
  }
}

void Link_Task(std::uint32_t nowMs)
{
  if (peripheral.isIdle() || ((nowMs - lastEdgeMs) <= BusIdleTimeoutMs))
  {
    return;
  }

  const IrqGuard guard;
  peripheral.reset();
  appliedDrive = Peripheral::Drive::Release;
  ReleaseData();
}

std::uint8_t Link_TakeUpdates(void)
{
  const IrqGuard guard;
  return peripheral.takeUpdates();
}

void Link_SetButtons(std::uint8_t mask)
{
  peripheral.setButtons(mask);
}

void Link_SetBatteryConnected(std::uint8_t connected)
{
  peripheral.setBatteryConnected(connected != 0U);
}

void Link_SetBatteryMillivolts(std::uint16_t millivolts)
{
  const IrqGuard guard;
  peripheral.setBatteryMillivolts(millivolts);
}

void Link_SetPressureAdc(std::uint16_t counts)
{
  const IrqGuard guard;
  peripheral.setPressureAdc(counts);
}

void Link_SetPressurizedTimeMs(std::uint32_t milliseconds)
{
  const IrqGuard guard;
  peripheral.setPressurizedTimeMs(milliseconds);
}

void Link_ReadDisplay(std::uint8_t *digits)
{
  const IrqGuard guard;
  const std::array<std::uint8_t, DisplayDigits> current = peripheral.display();

  for (std::size_t i = 0U; i < DisplayDigits; ++i)
  {
    digits[i] = current[i];
  }
}

std::uint16_t Link_ReadPumpDuty(void)
{
  const IrqGuard guard;
  return peripheral.pumpDuty();
}

std::uint8_t Link_ReadValveOpen(void)
{
  return peripheral.valveOpen() ? 1U : 0U;
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
