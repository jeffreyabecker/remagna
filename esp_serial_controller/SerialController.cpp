/**
  ******************************************************************************
  * @file    SerialController.cpp
  * @brief   Line-oriented serial front end for the link.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "SerialController.h"

#include <cstdlib>
#include <cstring>

SerialController::SerialController(Controller &controller, Stream &serial)
    : controller_(controller), serial_(serial)
{
}

void SerialController::begin()
{
  serial_.println();
  serial_.println(F("magna serial controller"));
  printHelp();
}

void SerialController::poll()
{
  while (serial_.available() > 0)
  {
    const int received = serial_.read();

    if (received < 0)
    {
      break;
    }

    if ((received == '\r') || (received == '\n'))
    {
      if (length_ > 0U)
      {
        line_[length_] = '\0';
        execute(line_);
        length_ = 0U;
      }
      continue;
    }

    if ((length_ + 1U) < sizeof(line_))
    {
      line_[length_++] = static_cast<char>(received);
    }
  }
}

void SerialController::execute(char *line)
{
  char *command = strtok(line, " \t");
  char *argument = strtok(nullptr, " \t");

  if (command == nullptr)
  {
    return;
  }

  if (strcmp(command, "help") == 0)
  {
    printHelp();
  }
  else if (strcmp(command, "info") == 0)
  {
    printInfo();
  }
  else if (strcmp(command, "buttons") == 0)
  {
    printButtons();
  }
  else if (strcmp(command, "battery") == 0)
  {
    printBattery();
  }
  else if (strcmp(command, "pressure") == 0)
  {
    printPressure();
  }
  else if (strcmp(command, "pump") == 0)
  {
    commandPump(argument);
  }
  else if (strcmp(command, "valve") == 0)
  {
    commandValve(argument);
  }
  else if (strcmp(command, "display") == 0)
  {
    commandDisplay(argument);
  }
  else
  {
    serial_.printf("unknown command '%s', try 'help'\n", command);
  }
}

void SerialController::printHelp()
{
  serial_.println(F("help                      this list"));
  serial_.println(F("info                      device id and map version"));
  serial_.println(F("buttons                   button mask, bit0..3 = button 1..4"));
  serial_.println(F("battery                   connected flag and millivolts"));
  serial_.println(F("pressure                  ADC counts and pressurized ms"));
  serial_.println(F("pump                      read the pump duty"));
  serial_.println(F("pump <0..1000>            write the pump duty, tenths of a percent"));
  serial_.println(F("valve                     read the valve"));
  serial_.println(F("valve open|closed         write the valve"));
  serial_.println(F("display                   read the six digits"));
  serial_.println(F("display <d0>..<d5>        write the digits, 0..15 or '-' to blank"));
}

void SerialController::printInfo()
{
  serial_.printf("device 0x%02X, map %u\n", static_cast<unsigned>(controller_.deviceId()), static_cast<unsigned>(controller_.mapVersion()));
}

void SerialController::printButtons()
{
  const uint8_t mask = controller_.buttons();

  serial_.printf("buttons 0x%02X", static_cast<unsigned>(mask));
  for (uint8_t index = 1U; index <= 4U; ++index)
  {
    serial_.printf(" %u", pressed(mask, index) ? 1U : 0U);
  }
  serial_.println();
}

void SerialController::printBattery()
{
  serial_.printf("battery %s, %u mV\n",
                 controller_.batteryConnected() ? "connected" : "absent",
                 static_cast<unsigned>(controller_.batteryMillivolts()));
}

void SerialController::printPressure()
{
  serial_.printf("pressure %u counts, %lu ms\n",
                 static_cast<unsigned>(controller_.pressureAdc()),
                 static_cast<unsigned long>(controller_.pressurizedTimeMs()));
}

void SerialController::commandPump(char *argument)
{
  if (argument == nullptr)
  {
    serial_.printf("pump %u\n", static_cast<unsigned>(controller_.pumpDuty()));
    return;
  }

  const long duty = strtol(argument, nullptr, 10);

  if ((duty < 0) || (duty > static_cast<long>(PumpDutyMax)))
  {
    serial_.printf("pump: expected 0..%u\n", static_cast<unsigned>(PumpDutyMax));
    return;
  }

  if (!controller_.writePumpDuty(static_cast<uint16_t>(duty)))
  {
    serial_.println(F("pump: rejected"));
    return;
  }

  serial_.printf("pump %u\n", static_cast<unsigned>(controller_.pumpDuty()));
}

void SerialController::commandValve(char *argument)
{
  if (argument == nullptr)
  {
    const std::optional<ValveState> state = controller_.valveState();

    if (!state.has_value())
    {
      serial_.println(F("valve unknown"));
    }
    else
    {
      serial_.printf("valve %s\n", (state.value() == ValveState::Open) ? "open" : "closed");
    }
    return;
  }

  if (strcmp(argument, "open") == 0)
  {
    controller_.writeValveState(ValveState::Open);
  }
  else if (strcmp(argument, "closed") == 0)
  {
    controller_.writeValveState(ValveState::Closed);
  }
  else
  {
    serial_.println(F("valve: expected open or closed"));
    return;
  }

  const std::optional<ValveState> state = controller_.valveState();
  serial_.printf("valve %s\n", (state.has_value() && (state.value() == ValveState::Open)) ? "open" : "closed");
}

void SerialController::commandDisplay(char *argument)
{
  if (argument == nullptr)
  {
    char digits[DisplayDigits] = {};
    controller_.display(digits);

    serial_.print(F("display"));
    for (uint8_t index = 0U; index < DisplayDigits; ++index)
    {
      serial_.printf(" %u", static_cast<unsigned>(static_cast<uint8_t>(digits[index])));
    }
    serial_.println();
    return;
  }

  char digits[DisplayDigits] = {};
  char *token = argument;

  for (uint8_t index = 0U; index < DisplayDigits; ++index)
  {
    if (token == nullptr)
    {
      serial_.println(F("display: expected six digits"));
      return;
    }

    if (strcmp(token, "-") == 0)
    {
      digits[index] = static_cast<char>(DigitBlank);
    }
    else
    {
      const long value = strtol(token, nullptr, 10);

      if ((value < 0) || (value > 15))
      {
        serial_.println(F("display: digits are 0..15 or '-'"));
        return;
      }
      digits[index] = static_cast<char>(value);
    }

    token = strtok(nullptr, " \t");
  }

  if (token != nullptr)
  {
    serial_.println(F("display: expected six digits"));
    return;
  }

  controller_.writeDisplay(digits);

  serial_.print(F("display"));
  for (uint8_t index = 0U; index < DisplayDigits; ++index)
  {
    serial_.printf(" %u", static_cast<unsigned>(static_cast<uint8_t>(digits[index])));
  }
  serial_.println();
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
