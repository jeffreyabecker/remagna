/**
  ******************************************************************************
  * @file    board.h
  * @brief   Board description for the PY32F002B TSSOP20 package device
  *          (PY32F002BF15P6).
  ******************************************************************************
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
  *       debugging.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __BOARD_H
#define __BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* TSSOP20 package pin number for every port pin -----------------------------*/
#define BOARD_TSSOP20_PIN_PA5   (1U)
#define BOARD_TSSOP20_PIN_PA6   (2U)
#define BOARD_TSSOP20_PIN_PA7   (3U)
#define BOARD_TSSOP20_PIN_PC0   (4U) /* PC0/NRST, SWDIO */
#define BOARD_TSSOP20_PIN_PC1   (5U)
#define BOARD_TSSOP20_PIN_PB7   (6U)
#define BOARD_TSSOP20_PIN_VSS   (7U)
#define BOARD_TSSOP20_PIN_PB6   (8U) /* PB6/SWD */
#define BOARD_TSSOP20_PIN_VCC   (9U)
#define BOARD_TSSOP20_PIN_PB5  (10U)
#define BOARD_TSSOP20_PIN_PB4  (11U)
#define BOARD_TSSOP20_PIN_PB3  (12U)
#define BOARD_TSSOP20_PIN_PB2  (13U)
#define BOARD_TSSOP20_PIN_PB1  (14U)
#define BOARD_TSSOP20_PIN_PB0  (15U)
#define BOARD_TSSOP20_PIN_PA0  (16U)
#define BOARD_TSSOP20_PIN_PA1  (17U)
#define BOARD_TSSOP20_PIN_PA2  (18U) /* PA2/SWCLK */
#define BOARD_TSSOP20_PIN_PA3  (19U)
#define BOARD_TSSOP20_PIN_PA4  (20U)

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
