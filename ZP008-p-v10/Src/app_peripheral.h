/**
  ******************************************************************************
  * @file    app_peripheral.h
  * @brief   Glue between the magna register map and the board I/O.
  *
  * The controller sees one register file (see bitbang_interface/src/protocol.h).
  * APP_PeripheralTask() keeps the input registers fresh from the board and
  * applies whatever the controller wrote since the previous call.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __APP_PERIPHERAL_H
#define __APP_PERIPHERAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exported functions prototypes ---------------------------------------------*/
/** Bring up the link and publish the first set of input values. */
void APP_PeripheralInit(void);

/** Poll the inputs, apply the pending outputs; call from the main loop. */
void APP_PeripheralTask(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PERIPHERAL_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
