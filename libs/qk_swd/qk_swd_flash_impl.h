/**
  ******************************************************************************
  * @file    qk_swd_flash_impl.h
  * @brief   Implementation of QkSwdFlash.
  *
  * Included by qk_swd_flash.h after the class declaration. Every method is
  * `this->` qualified: the frames, the hart control and the flash registers live
  * on base classes that depend on the port type, which unqualified lookup does
  * not search.
  ******************************************************************************
  */

#ifndef QK_SWD_FLASH_IMPL_H
#define QK_SWD_FLASH_IMPL_H

#include "qk_swd_flash.h"

/* ------------------------------------------------------------------------- */
/* Route 1: the RAM algorithm                                                */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::loadAlgorithm()
{
  /* The blob is copied as a word burst: it is position dependent code, so it has
   * to be at exactly the address it was linked for. */
  uint16_t words = static_cast<uint16_t>(target_.algorithmWords);
  QkSwdStatus status = this->writeMemoryBlock(target_.algorithm, target_.blob, words);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  if (words != target_.algorithmWords)
  {
    /* The burst stopped early, which means the target disappeared midway. */
    return QkSwdStatus::AlgorithmFault;
  }

  return syscall(target_.init, 0U, 0U, 0U, 0U);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::eraseChip()
{
  return syscall(target_.eraseChip, 0U, 0U, 0U, 0U);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::eraseSector(uint32_t address)
{
  return syscall(target_.eraseSector, address, 0U, 0U, 0U);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::program(uint32_t address, const uint32_t *words,
                                      uint32_t wordCount)
{
  if (words == nullptr)
  {
    return QkSwdStatus::Argument;
  }

  while (wordCount > 0U)
  {
    const uint32_t room = target_.bufferWords;
    const uint16_t chunk = static_cast<uint16_t>(wordCount > room ? room : wordCount);

    uint16_t staged = chunk;
    QkSwdStatus status = this->writeMemoryBlock(target_.programBuffer, words, staged);
    if (status != QkSwdStatus::Ok)
    {
      return status;
    }
    if (staged != chunk)
    {
      return QkSwdStatus::AlgorithmFault;
    }

    /* a1 is the byte count. The reference passes the whole buffer size here,
     * which would push stale words past a short final chunk, so this passes what
     * was actually staged instead. */
    status = syscall(target_.programPage, address, static_cast<uint32_t>(chunk) * 4U,
                     target_.programBuffer, 0U);
    if (status != QkSwdStatus::Ok)
    {
      return status;
    }

    address += static_cast<uint32_t>(chunk) * 4U;
    words += chunk;
    wordCount -= chunk;
  }
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::lockFlash()
{
  return syscall(target_.uninit, 0U, 0U, 0U, 0U);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::syscall(uint32_t entry, uint32_t arg0, uint32_t arg1,
                                      uint32_t arg2, uint32_t arg3)
{
  /* The routine is an ordinary function call as far as the target is concerned:
   * arguments in a0..a3, a stack of its own, and a return address that points at
   * an ebreak. dcsr is set so that ebreak enters the debug module instead of
   * raising an exception, which is how the call comes back to us. */
  QkSwdStatus status = this->writeGpr(10, arg0);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeGpr(11, arg1);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeGpr(12, arg2);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeGpr(13, arg3);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeGpr(2, target_.stackPointer);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeGpr(1, target_.breakpoint);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeCsr(QkSwdFlashDetail::CsrDpc, entry);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeCsr(QkSwdFlashDetail::CsrDcsr, QkSwdFlashDetail::DcsrEbreakToDebug);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  /* Run. The routine can finish before the running status is ever observed, so
   * a timeout from resume() is expected and not fatal; the halt is what proves
   * the callover. */
  this->resume();

  status = this->waitHalted();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  /* a0 carries the result. The first read after the halt can still come back
   * empty on a slow target, so give it a few tries like the reference does. */
  uint32_t result = 0;
  for (uint8_t attempt = 0; attempt < QkSwdFlashDetail::AlgorithmAttempts; ++attempt)
  {
    status = this->readGpr(10, result);
    if (status == QkSwdStatus::Ok)
    {
      break;
    }
    this->pauseNs(QkSwdFlashDetail::PollIntervalNs);
  }
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  return ((result & 0xFFU) != 0U) ? QkSwdStatus::AlgorithmFault : QkSwdStatus::Ok;
}

/* ------------------------------------------------------------------------- */
/* Route 2: the controller registers                                         */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::setFlashCtrl(uint32_t bits)
{
  uint32_t value = 0;
  const QkSwdStatus status = this->readMemory(QkSwdFlashDetail::FlashCtrl, value);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return this->writeMemory(QkSwdFlashDetail::FlashCtrl, value | bits);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::clearFlashCtrl(uint32_t bits)
{
  uint32_t value = 0;
  const QkSwdStatus status = this->readMemory(QkSwdFlashDetail::FlashCtrl, value);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return this->writeMemory(QkSwdFlashDetail::FlashCtrl, value & ~bits);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::waitFlashIdle()
{
  /* The controller sets BSY for the duration of an erase or a program; a page
   * takes milliseconds, a mass erase seconds. */
  uint32_t status = 0;

  for (uint32_t attempt = 0; attempt < QkSwdFlashDetail::FlashPollAttempts; ++attempt)
  {
    const QkSwdStatus read = this->readMemory(QkSwdFlashDetail::FlashStatus, status);
    if (read != QkSwdStatus::Ok)
    {
      return read;
    }
    if ((status & QkSwdFlashDetail::StatusBusy) == 0U)
    {
      return QkSwdStatus::Ok;
    }
    this->pauseNs(QkSwdFlashDetail::PollIntervalNs);
  }
  return QkSwdStatus::Timeout;
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::unlockFastMode()
{
  /* Two key registers with the same pair of keys: the normal one and the "mode"
   * one that opens the page buffer path on this part. */
  QkSwdStatus status = this->writeMemory(QkSwdFlashDetail::FlashKeyr, QkSwdFlashDetail::Key1);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashKeyr, QkSwdFlashDetail::Key2);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashModeKeyr, QkSwdFlashDetail::Key1);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return this->writeMemory(QkSwdFlashDetail::FlashModeKeyr, QkSwdFlashDetail::Key2);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::resetPageBuffer()
{
  QkSwdStatus status = setFlashCtrl(QkSwdFlashDetail::CtrlPageProgram);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = setFlashCtrl(QkSwdFlashDetail::CtrlBufferReset);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = waitFlashIdle();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return clearFlashCtrl(QkSwdFlashDetail::CtrlPageProgram);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::loadPageBuffer(uint32_t address, uint32_t value)
{
  /* Staging a word means: address it like normal memory while the page program
   * bit is set, then tell the controller to copy it into the page buffer. */
  QkSwdStatus status = setFlashCtrl(QkSwdFlashDetail::CtrlPageProgram);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(address, value);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = setFlashCtrl(QkSwdFlashDetail::CtrlBufferLoad);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = waitFlashIdle();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return clearFlashCtrl(QkSwdFlashDetail::CtrlPageProgram);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::erasePage(uint32_t address)
{
  QkSwdStatus status = setFlashCtrl(QkSwdFlashDetail::CtrlPageErase);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashAddress, address);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = setFlashCtrl(QkSwdFlashDetail::CtrlStart);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = waitFlashIdle();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return clearFlashCtrl(QkSwdFlashDetail::CtrlPageErase);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::writePage(uint32_t address)
{
  QkSwdStatus status = setFlashCtrl(QkSwdFlashDetail::CtrlPageProgram);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashAddress, address);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = setFlashCtrl(QkSwdFlashDetail::CtrlStart);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = waitFlashIdle();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return clearFlashCtrl(QkSwdFlashDetail::CtrlPageProgram);
}

/* ------------------------------------------------------------------------- */
/* Option bytes and reset                                                    */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::readOptionByte(uint32_t &value)
{
  return this->readMemory(QkSwdFlashDetail::FlashOptionByte, value);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::eraseOptionBytePage()
{
  QkSwdStatus status = unlockFastMode();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashOptKeyr, QkSwdFlashDetail::Key1);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashOptKeyr, QkSwdFlashDetail::Key2);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = setFlashCtrl(QkSwdFlashDetail::CtrlOptionErase);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = setFlashCtrl(QkSwdFlashDetail::CtrlStart);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = waitFlashIdle();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  /* Clear the end of operation flag (write one to clear), drop the erase bit and
   * release the option area, exactly like the reference does. */
  uint32_t value = 0;
  status = this->readMemory(QkSwdFlashDetail::FlashStatus, value);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashStatus,
                             value | QkSwdFlashDetail::StatusEndOfOperation);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = clearFlashCtrl(QkSwdFlashDetail::CtrlOptionErase);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return clearFlashCtrl(QkSwdFlashDetail::CtrlOptionLock);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::removeReadProtect()
{
  /* Read protection is one bit of the option byte. Clearing it means erasing the
   * option page, unlocking the option programming path and writing the reset
   * value back. The protection state only changes on the next reset, so the
   * caller still has to reset the target afterwards. */
  QkSwdStatus status = eraseOptionBytePage();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  for (uint8_t attempt = 0; attempt < QkSwdFlashDetail::ProtectAttempts; ++attempt)
  {
    status = unlockFastMode();
    if (status != QkSwdStatus::Ok)
    {
      return status;
    }
    status = this->writeMemory(QkSwdFlashDetail::FlashOptKeyr, QkSwdFlashDetail::Key1);
    if (status != QkSwdStatus::Ok)
    {
      return status;
    }
    status = this->writeMemory(QkSwdFlashDetail::FlashOptKeyr, QkSwdFlashDetail::Key2);
    if (status != QkSwdStatus::Ok)
    {
      return status;
    }
    status = setFlashCtrl(QkSwdFlashDetail::CtrlOptionProgram);
    if (status != QkSwdStatus::Ok)
    {
      return status;
    }

    /* A half word write into the option byte. The abstract command can fail
     * while the cell settles, which is what the retries are for. */
    status = this->writeMemory(QkSwdFlashDetail::FlashOptionByteData,
                               QkSwdFlashDetail::OptionByteResetValue, QkSwdWidth::Half);
    if (status == QkSwdStatus::Ok)
    {
      break;
    }
    this->pauseNs(QkSwdFlashDetail::PollIntervalNs);
  }
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  status = waitFlashIdle();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  /* End of operation is write one to clear, then drop the option program bit and
   * lock the option area again. */
  uint32_t value = 0;
  status = this->readMemory(QkSwdFlashDetail::FlashStatus, value);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = this->writeMemory(QkSwdFlashDetail::FlashStatus,
                             value | QkSwdFlashDetail::StatusEndOfOperation);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  status = clearFlashCtrl(QkSwdFlashDetail::CtrlOptionProgram);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }
  return clearFlashCtrl(QkSwdFlashDetail::CtrlOptionLock);
}

template <typename Port>
QkSwdStatus QkSwdFlash<Port>::requestSystemReset()
{
  /* The part has no debug module reset that reboots it, so the reset is asked
   * for through the NVIC's keyed control register. */
  return this->writeMemory(QkSwdFlashDetail::ResetControl,
                           QkSwdFlashDetail::ResetKey | QkSwdFlashDetail::ResetRequest);
}

#endif // QK_SWD_FLASH_IMPL_H