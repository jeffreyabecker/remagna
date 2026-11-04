/**
  ******************************************************************************
  * @file    app_swd.c
  * @brief   SWD debug port handover to the I2C1 pins.
  *
  * PB6 (SWDIO) and PA2 (SWCLK) come out of reset as debug pins. A debugger gets
  * APP_DEBUG_WINDOW_MS to attach; if none does, the pins are handed to I2C1.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_swd.h"
#include "i2c_device.h"

/* Private define ------------------------------------------------------------*/
#define APP_DEBUG_WINDOW_MS (10000U) /* Time reserved for a debugger to attach */
#define APP_DEBUG_POLL_MS   (50U)

/* Private variables ---------------------------------------------------------*/
static I2C_HandleTypeDef hi2c1;

/* Private function prototypes -----------------------------------------------*/
static uint8_t APP_DebuggerAttached(void);
static void APP_I2C1_Init(void);

/**
  * @brief  Wait for a debugger, then release PB6/PA2 to I2C1 if none attached.
  * @param  None
  * @retval None
  */
void APP_SwdHandover(void)
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
  * @brief  Initialize I2C1 as a slave on PB6 (SDA) and PA2 (SCL).
  * @note   HAL_I2C_MspInit() switches the pins to AF6 and releases SWD.
  * @param  None
  * @retval None
  */
static void APP_I2C1_Init(void)
{
  hi2c1.Instance             = I2C1;
  hi2c1.Init.ClockSpeed      = 100000U;
  hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1     = (uint16_t)(I2C_DEV_ADDRESS_7BIT << 1);
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  I2C_Device_Init();
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
