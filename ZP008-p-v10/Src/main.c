/**
  ******************************************************************************
  * @file    main.c
  * @brief   ZP008-p-v10 firmware: magna peripheral on a PY32F002B.
  *
  * The device is a peripheral on the bit-banged link: the controller clocks the
  * register file through CLK/DATA, the main loop keeps the input registers fresh
  * and applies the written outputs to the board.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_clock.h"
#include "app_io.h"
#include "app_peripheral.h"

/**
  * @brief  Main program.
  * @retval int
  */
int main(void)
{
  /* Reset of all peripherals, Initializes the Systick. */
  HAL_Init();

  /* System clock configuration */
  APP_ClockInit();

  /* Board I/O: LED now; display, buttons, ADC, motor and valve drivers later */
  APP_IO_Init();

  /* Bus pins, register file and the initial input values */
  APP_PeripheralInit();

  /* Nothing here blocks: the link is clocked entirely from the CLK interrupt */
  while (1)
  {
    APP_PeripheralTask();
    APP_IO_Task();
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
