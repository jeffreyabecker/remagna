/**
  ******************************************************************************
  * @file    board.h
  * @brief   ZP008-p-v10 board: HAL configuration, pin map and board bring-up.
  *
  * This is the only project header. It carries
  *   - the HAL module selection (normally py32f002b_hal_conf.h),
  *   - the oscillator and system values,
  *   - the TSSOP20 pin map and every pin the firmware uses,
  *   - the board bring-up, the interrupt vector names (normally
  *     py32f002b_it.h) and the clock configuration entry point.
  *
  * It stays plain C: board.c is a C translation unit even though the rest of
  * the firmware is C++.
  ******************************************************************************
  */

#ifndef __BOARD_H
#define __BOARD_H

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* ########################## Module Selection ############################## */
/**
  * @brief This is the list of modules to be used in the HAL driver
  *
  * The commented ones are the modules left to switch on once the remaining
  * board I/O (display, buttons, battery divider, pressure sensor, motor, valve)
  * gets its pins.
  */
#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
/* #define HAL_ADC_MODULE_ENABLED */
/* #define HAL_CRC_MODULE_ENABLED */
/* #define HAL_COMP_MODULE_ENABLED */
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
/* #define HAL_IWDG_MODULE_ENABLED */
/* #define HAL_TIM_MODULE_ENABLED */
/* #define HAL_LPTIM_MODULE_ENABLED */
#define HAL_PWR_MODULE_ENABLED
/* #define HAL_I2C_MODULE_ENABLED */
/* #define HAL_UART_MODULE_ENABLED */
/* #define HAL_USART_MODULE_ENABLED */
/* #define HAL_SPI_MODULE_ENABLED */
/* #define HAL_EXTI_MODULE_ENABLED */
#define HAL_CORTEX_MODULE_ENABLED

/* ###################### Oscillator Values adaptation ###################### */
#if !defined(HSI_VALUE)
#define HSI_VALUE ((uint32_t)24000000) /*!< Value of the Internal oscillator in Hz */
#endif

#if !defined(HSE_VALUE)
#define HSE_VALUE ((uint32_t)24000000) /*!< Value of the External oscillator in Hz */
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT ((uint32_t)200) /*!< Time out for HSE start up, in ms */
#endif

#if !defined(LSI_VALUE)
#define LSI_VALUE ((uint32_t)32768) /*!< LSI typical value in Hz */
#endif

#if !defined(LSE_VALUE)
#define LSE_VALUE ((uint32_t)32768) /*!< Value of the External oscillator in Hz */
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT ((uint32_t)5000) /*!< Time out for LSE start up, in ms */
#endif

/* ########################### System Configuration ######################### */
#define VDD_VALUE ((uint32_t)3300) /*!< Value of VDD in mV */
#define PRIORITY_HIGHEST 0
#define PRIORITY_HIGH 1
#define PRIORITY_LOW 2
#define PRIORITY_LOWEST 3
#define TICK_INT_PRIORITY ((uint32_t)PRIORITY_LOWEST) /*!< Tick interrupt priority */
#define USE_RTOS 0
#define PREFETCH_ENABLE 0

/* ########################## Assert Selection ############################## */
/* Uncomment to expand the assert_param macro in the HAL drivers. */
/* #define USE_FULL_ASSERT 1U */

/* ########################### HAL module headers ########################### */
/* py32f0xx_hal.h pulls the vendor include hook (py32f002b_hal_conf.h), which
 * includes the module headers selected above. It has to happen this way round:
 * the HAL header does not include the device header itself, so the module
 * headers must provide it before the HAL declarations are parsed. */
#include "py32f0xx_hal.h"

/* ############################# Assert macro ############################### */
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */

/* ############################## Pin map ################################### */
/**
  * The TSSOP20 package exposes every port pin of the PY32F002B:
  *
  *   pin  1  PA5        pin 11  PB4
  *   pin  2  PA6        pin 12  PB3
  *   pin  3  PA7        pin 13  PB2
  *   pin  4  PC0/NRST   pin 14  PB1
  *   pin  5  PC1        pin 15  PB0
  *   pin  6  PB7        pin 16  PA0
  *   pin  7  VSS        pin 17  PA1
  *   pin  8  PB6/SWD    pin 18  PA2/SWCLK
  *   pin  9  VCC        pin 19  PA3
  *   pin 10  PB5        pin 20  PA4
  *
  * Note: with the SWD option byte enabled (factory default) PC0 is the SWDIO
  *       pin and PA2 is the SWCLK pin, so PC0 cannot be used as GPIO while
  *       debugging. Keep both free if the board must stay flashable.
  */
#define BOARD_TSSOP20_PIN_PA5 (1U)
#define BOARD_TSSOP20_PIN_PA6 (2U)
#define BOARD_TSSOP20_PIN_PA7 (3U)
#define BOARD_TSSOP20_PIN_PC0 (4U) /* PC0/NRST, SWDIO */
#define BOARD_TSSOP20_PIN_PC1 (5U)
#define BOARD_TSSOP20_PIN_PB7 (6U)
#define BOARD_TSSOP20_PIN_VSS (7U)
#define BOARD_TSSOP20_PIN_PB6 (8U) /* PB6/SWD */
#define BOARD_TSSOP20_PIN_VCC (9U)
#define BOARD_TSSOP20_PIN_PB5 (10U)
#define BOARD_TSSOP20_PIN_PB4 (11U)
#define BOARD_TSSOP20_PIN_PB3 (12U)
#define BOARD_TSSOP20_PIN_PB2 (13U)
#define BOARD_TSSOP20_PIN_PB1 (14U)
#define BOARD_TSSOP20_PIN_PB0 (15U)
#define BOARD_TSSOP20_PIN_PA0 (16U)
#define BOARD_TSSOP20_PIN_PA1 (17U)
#define BOARD_TSSOP20_PIN_PA2 (18U) /* PA2/SWCLK */
#define BOARD_TSSOP20_PIN_PA3 (19U)
#define BOARD_TSSOP20_PIN_PA4 (20U)

/* Heartbeat LED: same pin as LED3 of the PY32F002B Start Kit, PA1 (pin 17). */
#define BOARD_LED_PORT GPIOA
#define BOARD_LED_PIN GPIO_PIN_1

/* Bit-banged link to the controller.
 *
 * CLK and DATA are the only pins the protocol needs; the rest of the board I/O
 * is not assigned yet on ZP008 rev 10. The pin numbers must stay in 0..7:
 * PY32F002B maps EXTI lines to EXTICR by pin number and only implements
 * EXTICR[0..1]. Moving CLK also changes which EXTI vector runs, which
 * Py32Peripheral.cpp and the prototypes below derive from BOARD_LINK_CLK_PIN_NUM.
 */
#define BOARD_LINK_CLK_PORT GPIOA
#define BOARD_LINK_CLK_PIN_NUM (0U) /* PA0, TSSOP20 pin 16 */
#define BOARD_LINK_CLK_PIN (1U << BOARD_LINK_CLK_PIN_NUM)

#define BOARD_LINK_DATA_PORT GPIOB
#define BOARD_LINK_DATA_PIN_NUM (1U) /* PB1, TSSOP20 pin 14 */
#define BOARD_LINK_DATA_PIN (1U << BOARD_LINK_DATA_PIN_NUM)

/* ############################ Board bring-up ############################## */
#ifdef __cplusplus
extern "C" {
#endif

/** Configure the system clock: HSI at 24 MHz, no dividers. */
void Board_InitClock(void);

/* The CLK pin number selects which EXTI vector serves the link. board.c only
 * supplies weak defaults for the interrupt handlers, so the application can
 * override any of them. */
#if (BOARD_LINK_CLK_PIN_NUM <= 1U)
#define BOARD_LINK_CLK_IRQHandler EXTI0_1_IRQHandler
#elif (BOARD_LINK_CLK_PIN_NUM <= 3U)
#define BOARD_LINK_CLK_IRQHandler EXTI2_3_IRQHandler
#else
#define BOARD_LINK_CLK_IRQHandler EXTI4_15_IRQHandler
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void BOARD_LINK_CLK_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
