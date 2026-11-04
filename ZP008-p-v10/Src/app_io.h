/**
  ******************************************************************************
  * @file    app_io.h
  * @brief   Board I/O access: LED, 7-segment display, buttons, battery,
  *          pressure sensor, motor and valve.
  *
  * These functions are the hardware boundary of the application. Only the
  * heartbeat LED is assigned on ZP008 rev 10 so far; the remaining pins are
  * still open, so their implementations in app_io.c are stubs.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __APP_IO_H
#define __APP_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define APP_IO_DISPLAY_DIGITS (6U)
#define APP_IO_DIGIT_BLANK    (0x10U) /* 0x00..0x0F show 0..F, 0x10 blanks */
#define APP_IO_PUMP_DUTY_MAX  (1000U) /* 0..1000 = 0.0..100.0 % */

/* Exported functions prototypes ---------------------------------------------*/
void APP_IO_Init(void);

/** Periodic hook: heartbeat LED now, display refresh once the panel is wired. */
void APP_IO_Task(void);

void APP_IO_DisplayWrite(const uint8_t digits[APP_IO_DISPLAY_DIGITS]);
uint8_t APP_IO_ReadButtons(void);
uint8_t APP_IO_ReadBatteryConnected(void);
uint16_t APP_IO_ReadBatteryMillivolts(void);
uint16_t APP_IO_ReadPressureAdc(void);
void APP_IO_ApplyMotorDuty(uint16_t duty);
void APP_IO_ApplyValveState(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IO_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
