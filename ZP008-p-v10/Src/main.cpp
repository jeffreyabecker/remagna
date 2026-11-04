/**
  ******************************************************************************
  * @file    main.cpp
  * @brief   ZP008-p-v10 firmware entry point.
  *
  * The device is a peripheral on the bit-banged link: the controller clocks the
  * register file through CLK/DATA, the main loop keeps the input registers fresh
  * and applies the written outputs to the board. Nothing here blocks; the link
  * is clocked entirely from the CLK interrupt.
  *
  * This file owns the peripheral instance and the CLK interrupt that serves it,
  * which overrides the weak stub in board.c.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <cstdint>

#include "board.h"
#include "Py32Peripheral.h"

namespace
{
constexpr std::uint32_t HeartbeatPeriodMs = 500U;

/* Constructed before main() runs, so the interrupt always finds a live object. */
Py32Peripheral peripheral;

std::uint32_t heartbeatMs = 0U;

void initLed()
{
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef led = {};
  led.Pin = BOARD_LED_PIN;
  led.Mode = GPIO_MODE_OUTPUT_PP;
  led.Pull = GPIO_NOPULL;
  led.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BOARD_LED_PORT, &led);

  HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_PIN_RESET);
  heartbeatMs = HAL_GetTick();
}

void heartbeat()
{
  const std::uint32_t now = HAL_GetTick();

  if ((now - heartbeatMs) >= HeartbeatPeriodMs)
  {
    heartbeatMs = now;
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
  }
}
} // namespace

/** CLK edge of the bit-banged link: hand the edge to the protocol engine. */
extern "C" void BOARD_LINK_CLK_IRQHandler(void)
{
  peripheral.onClockEdge();
}

int main(void)
{
  /* Reset of all peripherals, initializes the SysTick. */
  HAL_Init();

  Board_InitClock();

  initLed();
  peripheral.init();

  for (;;)
  {
    peripheral.poll();
    heartbeat();
  }
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
