/**
  ******************************************************************************
  * @file    main.c
  * @author  MCU Application Team
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "py32f002bxx_Start_Kit.h"

/* Private define ------------------------------------------------------------*/
#define APP_LED_TOGGLE_PERIOD_MS (500U)
#define APP_DEBUG_WINDOW_MS      (10000U) /* Time reserved for a debugger to attach */
#define APP_DEBUG_POLL_MS        (50U)
#define APP_I2C_ADDRESS_7BIT     (0x17U) /* This device; avoids 0x3C/0x3D displays */
#define APP_I2C_SLAVE_ADDRESS    (APP_I2C_ADDRESS_7BIT << 1)

/* Private variables ---------------------------------------------------------*/
static I2C_HandleTypeDef hi2c1;

/* Private user code ---------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
static void APP_SystemClockConfig(void);
static uint8_t APP_DebuggerAttached(void);
static void APP_ReserveSwdPinsForDebugger(void);
static void APP_I2C1_Init(void);

/**
  * @brief  Main program.
  * @retval int
  */
int main(void)
{
  /* Reset of all peripherals, Initializes the Systick. */
  HAL_Init();

  /* System clock configuration */
  APP_SystemClockConfig();

  /* LED3 of the Start Kit (PA1, TSSOP20 pin 17) */
  BSP_LED_Init(LED3);

  /* Keep PB6 (SWDIO) and PA2 (SWCLK) available for a while, then use them as
     I2C1 SDA/SCL if nothing is driving the debug interface. */
  APP_ReserveSwdPinsForDebugger();

  /* infinite loop */
  while (1)
  {
    BSP_LED_Toggle(LED3);
    HAL_Delay(APP_LED_TOGGLE_PERIOD_MS);
  }
}

/**
  * @brief  System clock configuration function
  * @param  None
  * @retval None
  */
static void APP_SystemClockConfig(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* Oscillator configuration */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE; /* Select oscillator HSE, HSI, LSI, LSE */
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                           /* Enable HSI */
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                           /* HSI 1 frequency division */
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;  /* Configure HSI clock 24MHz */
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS_DISABLE;               /* Close HSE bypass */
  RCC_OscInitStruct.LSIState = RCC_LSI_OFF;                          /* Close LSI */
  /*RCC_OscInitStruct.LSICalibrationValue = RCC_LSICALIBRATION_32768Hz;*/
  RCC_OscInitStruct.LSEState = RCC_LSE_OFF;                          /* Close LSE */
  /*RCC_OscInitStruct.LSEDriver = RCC_LSEDRIVE_MEDIUM;*/
  /* Configure oscillator */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  /* Clock source configuration */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1; /* Choose to configure clock HCLK, SYSCLK, PCLK1 */
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSISYS; /* Select HSISYS as the system clock */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;        /* AHB clock 1 division */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;         /* APB clock 1 division */
  /* Configure clock source */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    APP_ErrorHandler();
  }
}

/**
  * @brief  Detect an attached SWD probe.
  * @note   Cortex-M0+ does not expose DHCSR.C_DEBUGEN to firmware, so SWCLK is
  *         probed electrically instead: a probe actively drives SWCLK low,
  *         which overrides the internal pull-up. PA2 stays in SWD alternate
  *         function during the test, so an active debug session is not disturbed.
  * @retval 1 if a probe drives SWCLK, 0 otherwise
  */
static uint8_t APP_DebuggerAttached(void)
{
  uint32_t pupdr = GPIOA->PUPDR;
  uint8_t attached;

  MODIFY_REG(GPIOA->PUPDR, GPIO_PUPDR_PUPD2, GPIO_PUPDR_PUPD2_0);
  for (volatile uint32_t settle = 0U; settle < 1000U; settle++)
  {
    __NOP();
  }

  /* A pull-up wins only when nothing external drives the pin low. */
  attached = (READ_BIT(GPIOA->IDR, GPIO_IDR_ID2) == 0U) ? 1U : 0U;

  GPIOA->PUPDR = pupdr;
  return attached;
}

/**
  * @brief  Wait for a debugger, then release PB6/PA2 to I2C1 if none attached.
  * @param  None
  * @retval None
  */
static void APP_ReserveSwdPinsForDebugger(void)
{
  uint32_t start;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  start = HAL_GetTick();
  do
  {
    if (APP_DebuggerAttached() != 0U)
    {
      return;
    }
    HAL_Delay(APP_DEBUG_POLL_MS);
  } while ((HAL_GetTick() - start) < APP_DEBUG_WINDOW_MS);

  APP_I2C1_Init();
}

/**
  * @brief  Initialize I2C1 as a slave on PB6 (SDA) and PA2 (SCL).
  * @note   HAL_I2C_MspInit() switches the pins to AF6 and releases SWD.
  *         The 7-bit address is APP_I2C_ADDRESS_7BIT (0x17).
  * @param  None
  * @retval None
  */
static void APP_I2C1_Init(void)
{
  hi2c1.Instance             = I2C1;
  hi2c1.Init.ClockSpeed      = 100000U;
  hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1     = APP_I2C_SLAVE_ADDRESS;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    APP_ErrorHandler();
  }
}

/**
  * @brief  Error executing function.
  * @param  None
  * @retval None
  */
void APP_ErrorHandler(void)
{
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* Users can add their own printing information as needed,
     for example: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* Infinite loop */
  while (1)
  {
  }
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
