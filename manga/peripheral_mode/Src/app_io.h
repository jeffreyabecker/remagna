/**
  ******************************************************************************
  * @file    app_io.h
  * @brief   Board I/O access: LED, 7-segment display, buttons, battery,
  *          pressure sensor, motor and valve.
  *
  * The display, read and apply functions are the hardware boundary used by the
  * I2C register map. The stubs in app_io.c are where the real drivers go.
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

/* Exported functions prototypes ---------------------------------------------*/
void APP_IO_Init(void);
void APP_IO_ToggleLed(void);

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
