/**
  ******************************************************************************
  * @file    i2c_device.c
  * @brief   I2C1 slave register map for the peripheral_mode board.
  *
  * The register protocol is handled directly in the I2C interrupt instead of
  * the HAL listen state machine, because a register map needs variable-length
  * transfers and a repeated start without losing the register pointer.
  * HAL is only used to configure the peripheral (clock, timing, own address).
  * Hardware access is delegated to the APP_IO_* functions in app_io.c.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c_device.h"
#include "app_io.h"

/* Private define ------------------------------------------------------------*/
#define I2C_DEV_BUTTON_MASK  (0x0FU)
#define I2C_DEV_BATTERY_BIT  (0x01U)

/* Private variables ---------------------------------------------------------*/
static uint8_t display_digits[APP_IO_DISPLAY_DIGITS];
static uint16_t motor_duty;
static uint8_t valve_state;

/* Shadow values updated by register writes and applied on stop/repeated start */
static uint8_t display_dirty;
static uint8_t motor_dirty;
static uint8_t valve_dirty;

/* Read-only inputs sampled once per read transaction */
static uint8_t buttons;
static uint8_t battery_status;
static uint16_t battery_millivolts;
static uint16_t pressure_adc;

static uint8_t register_pointer;
static uint8_t expect_register_pointer = 1U;

/* Private function prototypes -----------------------------------------------*/
static uint8_t I2C_Device_ReadRegister(uint8_t reg);
static void I2C_Device_WriteRegister(uint8_t reg, uint8_t value);
static void I2C_Device_SnapshotInputs(void);
static void I2C_Device_Commit(void);
static uint8_t I2C_Device_NextRegister(uint8_t reg);

/**
  * @brief  Enable the register map on I2C1.
  * @note   Call after HAL_I2C_Init().
  * @param  None
  * @retval None
  */
void I2C_Device_Init(void)
{
  for (uint8_t i = 0U; i < APP_IO_DISPLAY_DIGITS; i++)
  {
    display_digits[i] = APP_IO_DIGIT_BLANK;
  }

  register_pointer = 0U;
  expect_register_pointer = 1U;
  display_dirty = 0U;
  motor_dirty = 0U;
  valve_dirty = 0U;

  /* Discard flags left over from configuration. */
  I2C1->SR1 = (uint32_t)~(I2C_SR1_AF | I2C_SR1_OVR | I2C_SR1_BERR | I2C_SR1_ARLO);

  SET_BIT(I2C1->CR1, I2C_CR1_ACK);
  SET_BIT(I2C1->CR2, I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN);

  HAL_NVIC_SetPriority(I2C1_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(I2C1_IRQn);
}

/**
  * @brief  I2C1 event and error interrupt handler.
  * @note   Called from I2C1_IRQHandler().
  * @param  None
  * @retval None
  */
void I2C_Device_IRQHandler(void)
{
  /* Reading SR1 then SR2 clears ADDR when it is set. */
  uint32_t sr1 = I2C1->SR1;
  uint32_t sr2 = I2C1->SR2;

  if ((sr1 & I2C_SR1_ADDR) != 0U)
  {
    if ((sr2 & I2C_SR2_TRA) != 0U)
    {
      /* Master reads: apply pending writes, then serve the register pointer. */
      I2C_Device_Commit();
      I2C_Device_SnapshotInputs();
      expect_register_pointer = 0U;
      I2C1->DR = I2C_Device_ReadRegister(register_pointer);
      register_pointer = I2C_Device_NextRegister(register_pointer);
    }
    else
    {
      /* Master writes: the first byte is the register pointer. */
      expect_register_pointer = 1U;
    }
  }
  else if ((sr1 & I2C_SR1_RXNE) != 0U)
  {
    uint8_t value = (uint8_t)I2C1->DR;

    if (expect_register_pointer != 0U)
    {
      register_pointer = value;
      expect_register_pointer = 0U;
    }
    else
    {
      I2C_Device_WriteRegister(register_pointer, value);
      register_pointer = I2C_Device_NextRegister(register_pointer);
    }
  }
  else if ((sr1 & I2C_SR1_AF) != 0U)
  {
    /* Master NACKed the last byte of a read; keep the pointer for next time. */
    I2C1->SR1 = (uint32_t)~I2C_SR1_AF;
    expect_register_pointer = 1U;
  }
  else if ((sr1 & I2C_SR1_TXE) != 0U)
  {
    I2C1->DR = I2C_Device_ReadRegister(register_pointer);
    register_pointer = I2C_Device_NextRegister(register_pointer);
  }
  else if ((sr1 & I2C_SR1_STOPF) != 0U)
  {
    I2C_Device_Commit();
    expect_register_pointer = 1U;
    (void)I2C1->SR1;
    SET_BIT(I2C1->CR1, I2C_CR1_PE);
  }
  else if ((sr1 & (I2C_SR1_OVR | I2C_SR1_BERR | I2C_SR1_ARLO)) != 0U)
  {
    I2C1->SR1 = (uint32_t)~(I2C_SR1_OVR | I2C_SR1_BERR | I2C_SR1_ARLO);
  }
  else
  {
    /* Spurious interrupt, nothing to do. */
  }
}

/**
  * @brief  Read one byte from the register map.
  * @param  reg Register offset
  * @retval Register value, 0 for unimplemented offsets
  */
static uint8_t I2C_Device_ReadRegister(uint8_t reg)
{
  if ((reg >= I2C_DEV_REG_DISPLAY_FIRST) &&
      (reg < (I2C_DEV_REG_DISPLAY_FIRST + APP_IO_DISPLAY_DIGITS)))
  {
    return display_digits[reg - I2C_DEV_REG_DISPLAY_FIRST];
  }

  switch (reg)
  {
    case I2C_DEV_REG_DEVICE_ID:
      return I2C_DEV_DEVICE_ID;
    case I2C_DEV_REG_MAP_VERSION:
      return I2C_DEV_MAP_VERSION;
    case I2C_DEV_REG_BUTTONS:
      return buttons;
    case I2C_DEV_REG_BATTERY_STATUS:
      return battery_status;
    case I2C_DEV_REG_BATTERY_MV_L:
      return (uint8_t)(battery_millivolts & 0xFFU);
    case I2C_DEV_REG_BATTERY_MV_H:
      return (uint8_t)(battery_millivolts >> 8);
    case I2C_DEV_REG_PRESSURE_ADC_L:
      return (uint8_t)(pressure_adc & 0xFFU);
    case I2C_DEV_REG_PRESSURE_ADC_H:
      return (uint8_t)(pressure_adc >> 8);
    case I2C_DEV_REG_MOTOR_DUTY_L:
      return (uint8_t)(motor_duty & 0xFFU);
    case I2C_DEV_REG_MOTOR_DUTY_H:
      return (uint8_t)(motor_duty >> 8);
    case I2C_DEV_REG_VALVE_STATE:
      return valve_state;
    default:
      return 0U;
  }
}

/**
  * @brief  Write one byte to the register map.
  * @note   Writes only update the shadow state; I2C_Device_Commit() applies it.
  * @param  reg Register offset
  * @param  value Value to write
  * @retval None
  */
static void I2C_Device_WriteRegister(uint8_t reg, uint8_t value)
{
  if ((reg >= I2C_DEV_REG_DISPLAY_FIRST) &&
      (reg < (I2C_DEV_REG_DISPLAY_FIRST + APP_IO_DISPLAY_DIGITS)))
  {
    if (value <= APP_IO_DIGIT_BLANK)
    {
      display_digits[reg - I2C_DEV_REG_DISPLAY_FIRST] = value;
      display_dirty = 1U;
    }
    return;
  }

  switch (reg)
  {
    case I2C_DEV_REG_MOTOR_DUTY_L:
      motor_duty = (uint16_t)((motor_duty & 0xFF00U) | value);
      if (motor_duty > I2C_DEV_MOTOR_DUTY_MAX)
      {
        motor_duty = I2C_DEV_MOTOR_DUTY_MAX;
      }
      motor_dirty = 1U;
      break;
    case I2C_DEV_REG_MOTOR_DUTY_H:
      motor_duty = (uint16_t)((motor_duty & 0x00FFU) | ((uint16_t)value << 8));
      if (motor_duty > I2C_DEV_MOTOR_DUTY_MAX)
      {
        motor_duty = I2C_DEV_MOTOR_DUTY_MAX;
      }
      motor_dirty = 1U;
      break;
    case I2C_DEV_REG_VALVE_STATE:
      if ((value == I2C_DEV_VALVE_CLOSED) || (value == I2C_DEV_VALVE_OPEN))
      {
        valve_state = value;
        valve_dirty = 1U;
      }
      break;
    default:
      /* Read-only or unimplemented registers ignore writes. */
      break;
  }
}

/**
  * @brief  Sample all read-only inputs for the next read transaction.
  * @param  None
  * @retval None
  */
static void I2C_Device_SnapshotInputs(void)
{
  buttons = (uint8_t)(APP_IO_ReadButtons() & I2C_DEV_BUTTON_MASK);
  battery_status = (uint8_t)((APP_IO_ReadBatteryConnected() != 0U) ? I2C_DEV_BATTERY_BIT : 0U);
  battery_millivolts = APP_IO_ReadBatteryMillivolts();
  pressure_adc = APP_IO_ReadPressureAdc();
}

/**
  * @brief  Push pending register writes to the hardware.
  * @note   Runs from interrupt context; keep the stubs short.
  * @param  None
  * @retval None
  */
static void I2C_Device_Commit(void)
{
  if (display_dirty != 0U)
  {
    display_dirty = 0U;
    APP_IO_DisplayWrite(display_digits);
  }
  if (motor_dirty != 0U)
  {
    motor_dirty = 0U;
    APP_IO_ApplyMotorDuty(motor_duty);
  }
  if (valve_dirty != 0U)
  {
    valve_dirty = 0U;
    APP_IO_ApplyValveState(valve_state);
  }
}

/**
  * @brief  Advance the register pointer with wrap-around.
  * @param  reg Current register offset
  * @retval Next register offset
  */
static uint8_t I2C_Device_NextRegister(uint8_t reg)
{
  reg++;
  return (reg >= I2C_DEV_REG_COUNT) ? 0U : reg;
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
