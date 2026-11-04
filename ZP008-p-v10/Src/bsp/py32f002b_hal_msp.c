/**
  ******************************************************************************
  * @file    py32f002b_hal_msp.c
  * @brief   MSP initialization for the HAL.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/**
  * @brief Initialize global MSP.
  */
void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
