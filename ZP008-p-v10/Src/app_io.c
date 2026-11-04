/**
  ******************************************************************************
  * @file    app_io.c
  * @brief   Board I/O access: LED, 7-segment display, buttons, battery,
  *          pressure sensor, motor and valve.
  *
  * The LED is the Start Kit LED pin (PA1). The display, button, battery, ADC,
  * motor and valve functions are stubs the application already calls: filling
  * them in is all it takes to bring the rest of the board up, no changes in the
  * link layer or the register map are needed.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_io.h"

/* Private define -----------------------------------------------------------*/
#define APP_IO_HEARTBEAT_PERIOD_MS (500U)

/* Private variables --------------------------------------------------------*/
static uint32_t heartbeatMs = 0U;

/**
  * @brief  Initialize the board I/O used by the application.
  * @param  None
  * @retval None
  */
void APP_IO_Init(void)
{
  /* Heartbeat LED: PA1, active high on the Start Kit */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef init = {0};
  init.Pin = BOARD_LED_PIN;
  init.Mode = GPIO_MODE_OUTPUT_PP;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BOARD_LED_PORT, &init);

  HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_PIN_RESET);
  heartbeatMs = HAL_GetTick();

  /* TODO(ZP008 rev 10): initialize buttons, display, ADC, motor PWM and valve
     outputs once the pins are assigned. */
}

/**
  * @brief  Periodic board I/O work, called from the main loop. Never blocks.
  * @param  None
  * @retval None
  */
void APP_IO_Task(void)
{
  const uint32_t now = HAL_GetTick();

  if ((now - heartbeatMs) >= APP_IO_HEARTBEAT_PERIOD_MS)
  {
    heartbeatMs = now;
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
  }
}

/**
  * @brief  Show digits on the 7-segment display.
  * @param  digits Digit values, 0x00..0x0F for 0..F and APP_IO_DIGIT_BLANK
  */
void APP_IO_DisplayWrite(const uint8_t digits[APP_IO_DISPLAY_DIGITS])
{
  (void)digits;
  /* TODO(ZP008 rev 10): convert the digits to segment patterns and refresh the
     display, e.g. from APP_IO_Task so the multiplexing stays non-blocking. */
}

/**
  * @brief  Read the four buttons.
  * @retval bit0..3 for button 1..4, 1 = pressed
  */
uint8_t APP_IO_ReadButtons(void)
{
  /* TODO(ZP008 rev 10): sample the button inputs. */
  return 0U;
}

/**
  * @brief  Report whether a battery is connected.
  * @retval 1 when connected, 0 otherwise
  */
uint8_t APP_IO_ReadBatteryConnected(void)
{
  /* TODO(ZP008 rev 10): sample the battery-detect input. */
  return 0U;
}

/**
  * @brief  Measure the battery voltage.
  * @retval Battery voltage in millivolts
  */
uint16_t APP_IO_ReadBatteryMillivolts(void)
{
  /* TODO(ZP008 rev 10): measure the battery divider with the ADC. */
  return 0U;
}

/**
  * @brief  Read the pressure sensor through the ADC.
  * @retval Raw ADC counts
  */
uint16_t APP_IO_ReadPressureAdc(void)
{
  /* TODO(ZP008 rev 10): start a conversion and return the raw ADC value. */
  return 0U;
}

/**
  * @brief  Apply the motor duty cycle.
  * @param  duty 0..1000, corresponding to 0.0..100.0 %
  */
void APP_IO_ApplyMotorDuty(uint16_t duty)
{
  (void)duty;
  /* TODO(ZP008 rev 10): update the motor PWM compare value. */
}

/**
  * @brief  Drive the valve.
  * @param  state 0 = closed, 1 = open
  */
void APP_IO_ApplyValveState(uint8_t state)
{
  (void)state;
  /* TODO(ZP008 rev 10): drive the valve output. */
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
