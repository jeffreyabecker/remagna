/**
  ******************************************************************************
  * @file    SerialController.h
  * @brief   Line-oriented serial front end for the link.
  *
  * Reads commands from a Stream, executes them against the Controller and
  * answers on the same stream. One command per line, whitespace separated:
  *
  *   help                      list the commands
  *   info                      device id and map version
  *   buttons                   button mask, bit0..3 = button 1..4
  *   battery                   connected flag and millivolts
  *   pressure                  ADC counts and pressurized milliseconds
  *   pump                      read the pump duty
  *   pump <0..1000>            write the pump duty (tenths of a percent)
  *   valve                     read the valve, open or closed
  *   valve open|closed         write the valve
  *   display                   read the six digits
  *   display <d0> .. <d5>      write the six digits, 0..15 or '-' to blank
  *
  * poll() never blocks: it only executes complete lines.
  ******************************************************************************
  */

#ifndef SERIALCONTROLLER_H
#define SERIALCONTROLLER_H

/* Includes ------------------------------------------------------------------*/
#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include <protocol.h>

class SerialController
{
public:
  SerialController(Controller &controller, Stream &serial);

  /** Greet the terminal and print the command list. */
  void begin();

  /** Consume the bytes available on the stream and run every complete line. */
  void poll();

private:
  void execute(char *line);
  void printHelp();

  void printInfo();
  void printButtons();
  void printBattery();
  void printPressure();
  void commandPump(char *argument);
  void commandValve(char *argument);
  void commandDisplay(char *argument);

  Controller &controller_;
  Stream &serial_;

  char line_[64] = {};
  std::size_t length_ = 0U;
};

#endif // SERIALCONTROLLER_H

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
