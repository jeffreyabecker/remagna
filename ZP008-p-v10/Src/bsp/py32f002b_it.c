/**
  ******************************************************************************
  * @file    py32f002b_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "py32f002b_it.h"
#include "link.h"

/******************************************************************************/
/*          Cortex-M0+ Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* PY32F002B Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file.                                          */
/******************************************************************************/

/**
  * @brief CLK edge of the bit-banged link: hand the edge to the protocol engine.
  * @note  The vector depends on the CLK pin number (see board.h); only the EXTI
  *        handler for that line may be defined here.
  */
#if (BOARD_LINK_CLK_PIN_NUM <= 1U)
void EXTI0_1_IRQHandler(void)
#elif (BOARD_LINK_CLK_PIN_NUM <= 3U)
void EXTI2_3_IRQHandler(void)
#else
void EXTI4_15_IRQHandler(void)
#endif
{
  Link_OnClockEdge();
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
