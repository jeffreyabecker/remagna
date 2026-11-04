/**
  ******************************************************************************
  * @file    protocol.h
  * @brief   Wire definition shared by the controller and peripheral engines.
  *
  * Frame (controller-driven, LSB-first):
  *
  *   START     1 bit, DATA driven low
  *   COMMAND   8 bits, bit0 = R/W (0 = write, 1 = read), bits 1..7 reserved
  *   ADDRESS   8 bits, first register offset
  *   LENGTH    8 bits, number of data bytes (1..255)
  *   write:    DATA, controller drives
  *   read:     TURN (controller releases DATA), then DATA driven by peripheral
  *
  * Registers auto-increment across LENGTH bytes. CLK idles low, DATA idles high.
  * The controller releases DATA at the end of every transaction.
  ******************************************************************************
  */

#ifndef REMAGNA_BITBANG_PROTOCOL_H
#define REMAGNA_BITBANG_PROTOCOL_H

#include <cstdint>

namespace remagna::bitbang::protocol
{

inline constexpr std::uint8_t kBitsPerByte = 8U;

inline constexpr bool kStartBit = false;

inline constexpr std::uint8_t kCommandWrite = 0x00U;
inline constexpr std::uint8_t kCommandRead = 0x01U;
inline constexpr std::uint8_t kCommandReadMask = 0x01U;

} // namespace remagna::bitbang::protocol

#endif // REMAGNA_BITBANG_PROTOCOL_H
