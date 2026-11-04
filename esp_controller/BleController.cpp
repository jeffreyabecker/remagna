/**
  ******************************************************************************
  * @file    BleController.cpp
  * @brief   BLE front end for the link: one GATT service mirroring the register
  *          map.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "BleController.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace
{
constexpr const char *DeviceName = "magna-controller";

constexpr const char *ServiceUuid = "fff0";
constexpr const char *DeviceInfoUuid = "fff1";
constexpr const char *ButtonsUuid = "fff2";
constexpr const char *BatteryUuid = "fff3";
constexpr const char *PressureUuid = "fff4";
constexpr const char *PumpDutyUuid = "fff5";
constexpr const char *ValveUuid = "fff6";
constexpr const char *DisplayUuid = "fff7";

/* Filled by the NimBLE host task, drained by takeWrites(). */
portMUX_TYPE writesLock = portMUX_INITIALIZER_UNLOCKED;
BleWrites writes;

NimBLECharacteristic *deviceInfoCharacteristic = nullptr;
NimBLECharacteristic *buttonsCharacteristic = nullptr;
NimBLECharacteristic *batteryCharacteristic = nullptr;
NimBLECharacteristic *pressureCharacteristic = nullptr;
NimBLECharacteristic *pumpDutyCharacteristic = nullptr;
NimBLECharacteristic *valveCharacteristic = nullptr;
NimBLECharacteristic *displayCharacteristic = nullptr;

void putUint16(uint8_t *out, uint16_t value)
{
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void putUint32(uint8_t *out, uint32_t value)
{
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

uint16_t getUint16(const uint8_t *in)
{
  return static_cast<uint16_t>(static_cast<uint16_t>(in[0]) |
                               (static_cast<uint16_t>(in[1]) << 8));
}

/** Queue a client write; nothing here touches the bit-banged link. */
class WriteCallbacks final : public NimBLECharacteristicCallbacks
{
public:
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override
  {
    (void)connInfo;

    const NimBLEAttValue value = characteristic->getValue();
    const uint8_t *data = value.data();
    const size_t length = value.length();

    portENTER_CRITICAL(&writesLock);

    if ((characteristic == pumpDutyCharacteristic) && (length >= 2U))
    {
      writes.pumpDutyValue = getUint16(data);
      writes.pumpDuty = true;
    }
    else if ((characteristic == valveCharacteristic) && (length >= 1U))
    {
      writes.valveOpen = data[0] != 0U;
      writes.valve = true;
    }
    else if ((characteristic == displayCharacteristic) && (length >= DisplayDigits))
    {
      for (size_t i = 0U; i < DisplayDigits; ++i)
      {
        writes.displayDigits[i] = static_cast<char>(data[i]);
      }
      writes.display = true;
    }

    portEXIT_CRITICAL(&writesLock);
  }
};

WriteCallbacks writeCallbacks;
} // namespace

void BleController::begin()
{
  NimBLEDevice::init(DeviceName);

  NimBLEServer *server = NimBLEDevice::createServer();
  NimBLEService *service = server->createService(ServiceUuid);

  deviceInfoCharacteristic = service->createCharacteristic(DeviceInfoUuid, NIMBLE_PROPERTY::READ);
  buttonsCharacteristic = service->createCharacteristic(ButtonsUuid, NIMBLE_PROPERTY::READ);
  batteryCharacteristic = service->createCharacteristic(BatteryUuid, NIMBLE_PROPERTY::READ);
  pressureCharacteristic = service->createCharacteristic(PressureUuid, NIMBLE_PROPERTY::READ);

  pumpDutyCharacteristic =
      service->createCharacteristic(PumpDutyUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  valveCharacteristic =
      service->createCharacteristic(ValveUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  displayCharacteristic =
      service->createCharacteristic(DisplayUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);

  pumpDutyCharacteristic->setCallbacks(&writeCallbacks);
  valveCharacteristic->setCallbacks(&writeCallbacks);
  displayCharacteristic->setCallbacks(&writeCallbacks);

  /* The service starts together with the server. */

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(ServiceUuid);
  advertising->setName(DeviceName);
  advertising->start();
}

void BleController::publishDeviceInfo(uint8_t deviceId, uint8_t mapVersion)
{
  const uint8_t value[2] = {deviceId, mapVersion};
  deviceInfoCharacteristic->setValue(value, sizeof(value));
}

void BleController::publishButtons(uint8_t mask)
{
  buttonsCharacteristic->setValue(&mask, 1U);
}

void BleController::publishBattery(bool connected, uint16_t millivolts)
{
  uint8_t value[3] = {};
  value[0] = connected ? 1U : 0U;
  putUint16(&value[1], millivolts);
  batteryCharacteristic->setValue(value, sizeof(value));
}

void BleController::publishPressure(uint16_t adc, uint32_t pressurizedMilliseconds)
{
  uint8_t value[6] = {};
  putUint16(&value[0], adc);
  putUint32(&value[2], pressurizedMilliseconds);
  pressureCharacteristic->setValue(value, sizeof(value));
}

void BleController::publishPumpDuty(uint16_t tenthsPercent)
{
  uint8_t value[2] = {};
  putUint16(value, tenthsPercent);
  pumpDutyCharacteristic->setValue(value, sizeof(value));
}

void BleController::publishValve(bool open)
{
  const uint8_t value = open ? 1U : 0U;
  valveCharacteristic->setValue(&value, 1U);
}

void BleController::publishDisplay(const char digits[DisplayDigits])
{
  displayCharacteristic->setValue(reinterpret_cast<const uint8_t *>(digits), DisplayDigits);
}

BleWrites BleController::takeWrites()
{
  BleWrites pending;

  portENTER_CRITICAL(&writesLock);
  pending = writes;
  writes = BleWrites{};
  portEXIT_CRITICAL(&writesLock);

  return pending;
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
