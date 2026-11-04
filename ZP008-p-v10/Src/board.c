/**
  ******************************************************************************
  * @file    board.c
  * @brief   ZP008-p-v10 board bring-up: clock, HAL MSP and interrupt vectors.
  *
  * The only C translation unit left in the firmware. It holds everything that
  * has to run before (or independently of) the C++ peripheral object: the clock
  * tree, the SYSCFG/PWR setup the HAL asks for, and the vectors. The CLK edge
  * vector hands straight over to Board_OnClockEdge() in peripheral.cpp.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "board.h"

/* Private functions --------------------------------------------------------*/
static void halt(void)
{
  for (;;)
  {
  }
}

/* Exported functions -------------------------------------------------------*/
void Board_InitClock(void)
{
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clocks = {0};

  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  oscillator.HSIState = RCC_HSI_ON;
  oscillator.HSIDiv = RCC_HSI_DIV1;
  oscillator.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
  oscillator.HSEState = RCC_HSE_BYPASS_DISABLE;
  oscillator.LSIState = RCC_LSI_OFF;
  oscillator.LSEState = RCC_LSE_OFF;

  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
  {
    halt();
  }

  clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  clocks.SYSCLKSource = RCC_SYSCLKSOURCE_HSISYS;
  clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clocks.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_0) != HAL_OK)
  {
    halt();
  }
}

/** Global MSP setup, called by HAL_Init(). */
void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

/******************************************************************************/
/* Cortex-M0+ processor interrupts and exceptions                             */
/*                                                                            */
/* Weak defaults: an application file can replace any of them.                */
/******************************************************************************/
__weak void NMI_Handler(void)
{
}

__weak void HardFault_Handler(void)
{
  halt();
}

__weak void SVC_Handler(void)
{
}

__weak void PendSV_Handler(void)
{
}

__weak void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* Peripheral interrupts                                                      */
/******************************************************************************/

/** CLK edge of the bit-banged link; the peripheral owns it, not the board. */
__weak void BOARD_LINK_CLK_IRQHandler(void)
{
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;

  halt();
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
