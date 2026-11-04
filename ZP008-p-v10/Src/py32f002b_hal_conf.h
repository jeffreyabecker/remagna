/**
  ******************************************************************************
  * @file    py32f002b_hal_conf.h
  * @brief   Vendor include hook: the HAL configuration lives in board.h.
  *
  * py32f0xx_hal.h does #include "py32f002b_hal_conf.h" as its very first
  * include, before any HAL or device type exists. board.h therefore cannot
  * provide this file's body: it is already mid-include whenever an application
  * translation unit starts the chain, and its include guard would turn this
  * into a no-op.
  *
  * So the module selection, the system values and the assert macro stay in
  * board.h, and only the "include the selected module headers" list lives here,
  * where the HAL expects it.
  ******************************************************************************
  */

#ifndef __PY32F002B_HAL_CONF_H
#define __PY32F002B_HAL_CONF_H

/* No-op when board.h started the chain; otherwise it is what defines the
 * HAL_*_MODULE_ENABLED macros used below. */
#include "board.h"

#ifdef HAL_RCC_MODULE_ENABLED
#include "py32f002b_hal_rcc.h"
#endif

#ifdef HAL_EXTI_MODULE_ENABLED
#include "py32f002b_hal_exti.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
#include "py32f002b_hal_gpio.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "py32f002b_hal_cortex.h"
#endif

#ifdef HAL_ADC_MODULE_ENABLED
#include "py32f002b_hal_adc.h"
#endif

#ifdef HAL_CRC_MODULE_ENABLED
#include "py32f002b_hal_crc.h"
#endif

#ifdef HAL_COMP_MODULE_ENABLED
#include "py32f002b_hal_comp.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
#include "py32f002b_hal_flash.h"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
#include "py32f002b_hal_i2c.h"
#endif

#ifdef HAL_IWDG_MODULE_ENABLED
#include "py32f002b_hal_iwdg.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
#include "py32f002b_hal_pwr.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
#include "py32f002b_hal_spi.h"
#endif

#ifdef HAL_TIM_MODULE_ENABLED
#include "py32f002b_hal_tim.h"
#endif

#ifdef HAL_LPTIM_MODULE_ENABLED
#include "py32f002b_hal_lptim.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
#include "py32f002b_hal_uart.h"
#endif

#ifdef HAL_USART_MODULE_ENABLED
#include "py32f002b_hal_usart.h"
#endif

#endif /* __PY32F002B_HAL_CONF_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
