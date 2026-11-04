/**
  ******************************************************************************
  * @file    app_peripheral.c
  * @brief   Glue between the magna register map and the board I/O.
  *
  * Keeps the read-only registers current (buttons, battery, pressure, the
  * pressurised time counter) and pushes the written registers out to the board
  * (display digits, pump duty, valve). The link layer only reports what changed,
  * so the board drivers are called with the exact values the controller wrote.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_io.h"
#include "app_peripheral.h"
#include "link.h"

/* Private define -----------------------------------------------------------*/
/* Pressure ADC count that means "pressurised"; tune to the sensor. */
#define APP_PERIPHERAL_PRESSURE_THRESHOLD (512U)

/* Private variables --------------------------------------------------------*/
static uint32_t pressurizedMs = 0U;
static uint32_t pressureSampleMs = 0U;

/* Private functions --------------------------------------------------------*/
/** Accumulate the time the system spends pressurised. */
static void APP_PeripheralUpdatePressure(uint16_t pressureAdc)
{
  const uint32_t now = HAL_GetTick();
  const uint32_t elapsed = now - pressureSampleMs;
  pressureSampleMs = now;

  if (pressureAdc >= APP_PERIPHERAL_PRESSURE_THRESHOLD)
  {
    pressurizedMs += elapsed;
  }
  else
  {
    pressurizedMs = 0U;
  }
}

/** Publish the current board state to the registers the controller reads. */
static void APP_PeripheralPollInputs(void)
{
  const uint16_t pressureAdc = APP_IO_ReadPressureAdc();

  APP_PeripheralUpdatePressure(pressureAdc);

  Link_SetButtons(APP_IO_ReadButtons());
  Link_SetBatteryConnected(APP_IO_ReadBatteryConnected());
  Link_SetBatteryMillivolts(APP_IO_ReadBatteryMillivolts());
  Link_SetPressureAdc(pressureAdc);
  Link_SetPressurizedTimeMs(pressurizedMs);
}

/** Apply whatever the controller wrote since the last call. */
static void APP_PeripheralApplyOutputs(void)
{
  const uint8_t updates = Link_TakeUpdates();

  if ((updates & LINK_UPDATE_DISPLAY) != 0U)
  {
    uint8_t digits[APP_IO_DISPLAY_DIGITS];
    Link_ReadDisplay(digits);
    APP_IO_DisplayWrite(digits);
  }

  if ((updates & LINK_UPDATE_PUMP_DUTY) != 0U)
  {
    APP_IO_ApplyMotorDuty(Link_ReadPumpDuty());
  }

  if ((updates & LINK_UPDATE_VALVE_STATE) != 0U)
  {
    APP_IO_ApplyValveState(Link_ReadValveOpen());
  }
}

/* Exported functions -------------------------------------------------------*/
void APP_PeripheralInit(void)
{
  Link_Init();

  pressureSampleMs = HAL_GetTick(); /* seed the millis baseline */
  APP_PeripheralPollInputs();
}

void APP_PeripheralTask(void)
{
  Link_Task(HAL_GetTick());

  APP_PeripheralPollInputs();
  APP_PeripheralApplyOutputs();
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
