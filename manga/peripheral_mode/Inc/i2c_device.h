/**
  ******************************************************************************
  * @file    i2c_device.h
  * @brief   I2C1 slave register map for the peripheral_mode board.
  *
  * Register pointer protocol:
  *   1. Master writes one byte: the register offset (table below).
  *   2. Master either writes data bytes (auto-increment) or issues a repeated
  *      start and reads data bytes (auto-increment).
  *
  * Multi-byte values are little-endian. Registers not listed return 0 and
  * ignore writes.
  *
  *   0x00  DEVICE_ID          R    ASCII 'M'
  *   0x01  MAP_VERSION        R    register map version
  *   0x02  DISPLAY_DIGIT1     R/W  digit value, see I2C_DEV_DIGIT_BLANK
  *   0x03  DISPLAY_DIGIT2     R/W
  *   0x04  DISPLAY_DIGIT3     R/W
  *   0x05  DISPLAY_DIGIT4     R/W
  *   0x06  DISPLAY_DIGIT5     R/W
  *   0x07  DISPLAY_DIGIT6     R/W
  *   0x08  BUTTONS            R    bit0..3 = button 1..4, 1 = pressed
  *   0x09  BATTERY_STATUS     R    bit0 = battery connected
  *   0x0A  BATTERY_MV_L       R    battery voltage in mV
  *   0x0B  BATTERY_MV_H       R
  *   0x0C  PRESSURE_ADC_L     R    raw pressure ADC counts
  *   0x0D  PRESSURE_ADC_H     R
  *   0x0E  MOTOR_DUTY_L       R/W  motor duty, 0..1000 = 0.0..100.0 %
  *   0x0F  MOTOR_DUTY_H       R/W
  *   0x10  VALVE_STATE        R/W  0 = closed, 1 = open
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __I2C_DEVICE_H
#define __I2C_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define I2C_DEV_REG_DEVICE_ID      (0x00U)
#define I2C_DEV_REG_MAP_VERSION    (0x01U)
#define I2C_DEV_REG_DISPLAY_FIRST  (0x02U)
#define I2C_DEV_REG_BUTTONS        (0x08U)
#define I2C_DEV_REG_BATTERY_STATUS (0x09U)
#define I2C_DEV_REG_BATTERY_MV_L   (0x0AU)
#define I2C_DEV_REG_BATTERY_MV_H   (0x0BU)
#define I2C_DEV_REG_PRESSURE_ADC_L (0x0CU)
#define I2C_DEV_REG_PRESSURE_ADC_H (0x0DU)
#define I2C_DEV_REG_MOTOR_DUTY_L   (0x0EU)
#define I2C_DEV_REG_MOTOR_DUTY_H   (0x0FU)
#define I2C_DEV_REG_VALVE_STATE    (0x10U)

#define I2C_DEV_REG_COUNT          (0x11U)

#define I2C_DEV_DEVICE_ID          (0x4DU) /* ASCII 'M' */
#define I2C_DEV_MAP_VERSION        (0x01U)

#define I2C_DEV_DIGIT_BLANK        (0x10U) /* 0x00..0x0F show 0..F, 0x10 blanks */

#define I2C_DEV_MOTOR_DUTY_MAX     (1000U)

#define I2C_DEV_VALVE_CLOSED       (0x00U)
#define I2C_DEV_VALVE_OPEN         (0x01U)

/* Exported functions prototypes ---------------------------------------------*/
void I2C_Device_Init(void);
void I2C_Device_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_DEVICE_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
