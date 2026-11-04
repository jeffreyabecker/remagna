/**
  ******************************************************************************
  * @file    link.h
  * @brief   C entry points for the bit-banged link to the magna controller.
  *
  * link.cpp owns the protocol engine (bitbang_interface/src/protocol.h) and the
  * two bus pins. The CLK EXTI interrupt feeds it one edge at a time and applies
  * the DATA drive state it returns; the main loop publishes the input registers
  * and consumes the update flags. Nothing here blocks.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LINK_H
#define __LINK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
/* Output registers written since the last Link_TakeUpdates(); these mirror
 * Peripheral::Update in protocol.h. */
#define LINK_UPDATE_DISPLAY      (0x01U)
#define LINK_UPDATE_PUMP_DUTY    (0x02U)
#define LINK_UPDATE_VALVE_STATE  (0x04U)

/* Register value semantics, mirroring protocol.h. */
#define LINK_DISPLAY_DIGITS      (6U)
#define LINK_DIGIT_BLANK         (0x10U) /* 0x00..0x0F show 0..F, 0x10 blanks */
#define LINK_PUMP_DUTY_MAX       (1000U) /* 0..1000 = 0.0..100.0 % */

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Configure the CLK/DATA pins and the CLK edge interrupt.
  * @note   Safe to call again: it drops any partial frame in progress.
  */
void Link_Init(void);

/**
  * @brief  Advance the protocol engine by one CLK edge.
  * @note   Call from the CLK EXTI handler only.
  */
void Link_OnClockEdge(void);

/**
  * @brief  Drop a partial frame once the controller stopped clocking.
  * @param  nowMs Current HAL tick, used for the bus idle timeout.
  */
void Link_Task(uint32_t nowMs);

/** Pending update flags; the returned bits are cleared. */
uint8_t Link_TakeUpdates(void);

/* Inputs: values the controller reads. */
void Link_SetButtons(uint8_t mask);
void Link_SetBatteryConnected(uint8_t connected);
void Link_SetBatteryMillivolts(uint16_t millivolts);
void Link_SetPressureAdc(uint16_t counts);
void Link_SetPressurizedTimeMs(uint32_t milliseconds);

/* Outputs: values the controller wrote. */
void Link_ReadDisplay(uint8_t digits[LINK_DISPLAY_DIGITS]);
uint16_t Link_ReadPumpDuty(void);
uint8_t Link_ReadValveOpen(void);

#ifdef __cplusplus
}
#endif

#endif /* __LINK_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
