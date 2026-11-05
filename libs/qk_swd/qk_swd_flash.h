/**
 ******************************************************************************
 * @file    qk_swd_flash.h
 * @brief   Flash programming for CH32V003 class targets, on top of qk_swd.
 *
 * The debug module can read and write RAM, and nothing else. Everything a
 * debugger needs to do to flash - unlock the controller, load the page buffer,
 * start the erase, wait for the busy flag - therefore has to run *on the
 * target*, which leaves two routes:
 *
 * ROUTE 1 - RAM ALGORITHM (what the reference programmer does)
 * -----------------------------------------------------------
 * A position independent blob of RISC-V code (~430 bytes) is copied into
 * target SRAM, and this layer calls into it through the debug module:
 *
 *   1. write the arguments to a0..a3,
 *   2. write sp and ra (ra points at an ebreak),
 *   3. write dpc = entry, dcsr = 0x9603 so an ebreak drops into the debug
 *      module instead of trapping,
 *   4. resume, wait until the hart halts again on that ebreak,
 *   5. read a0: zero means the routine worked, anything else is its error.
 *
 * That is the whole RISC-V "syscall" mechanism used by every OpenOCD flash
 * driver: no protocol of its own, just registers and a breakpoint. The blob is
 * QkSwdFlashDetail::Algorithm, its RAM layout is in the table below, and the
 * addresses in QkSwdFlashDetail::ReferenceTarget are the ones it was linked
 * for - it is not relocatable, so the target struct has to match.
 *
 *   0x20000000  ebreak          ra value, the routine's return address
 *   0x20000004  init            unlock the controller, at function start
 *   0x20000028  uninit          lock the controller again
 *   0x20000038  eraseChip       whole flash
 *   0x2000006a  eraseSector     a0 = address
 *   0x200000a2  programPage     a0 = address, a1 = bytes, a2 = buffer
 *   0x20000200  staging buffer  1024 bytes of words to program
 *   0x20000800  stack top       sp value
 *
 * ROUTE 2 - CONTROLLER REGISTERS (the reference's "fast mode")
 * -----------------------------------------------------------
 * For a single page, the debugger can drive the flash controller directly with
 * ordinary memory writes: unlock with the keys, put a word in the page buffer,
 * load it, set the address and start bit, poll the busy flag. No RAM code, no
 * syscall, but only the page sized operations are available and every write
 * costs a full abstract command round trip.
 *
 * Either way the hart must be halted first, and after programming the target
 * needs a reset before it runs the new code.
 *
 * TYPICAL SEQUENCE (this is what the reference's main.c does)
 * ----------------------------------------------------------
 *   probe(); halt();                     bring the debug module up, stop the hart
 *   readOptionByte(obr);                 obr bit 0 set means read protected:
 *   removeReadProtect(); reset; halt();    clear it, reset, stop again
 *   loadAlgorithm();                     copy the blob, run its init entry
 *   eraseChip();                         or eraseSector(address) per sector
 *   program(0x08000000, image, words);   staged through the algorithm buffer
 *   lockFlash();                         re-lock the controller
 *   requestSystemReset();                and let the new code run
 ******************************************************************************
 */

#ifndef QK_SWD_FLASH_H
#define QK_SWD_FLASH_H

#include <stdint.h>

#include "qk_swd.h"

/**
 * Where the RAM algorithm lives and what its entry points are.
 *
 * Mirrors the reference programmer's program_target_t, with the blob kept as
 * words instead of bytes.
 */
struct QkSwdFlashTarget
{
  uint32_t init;           /**< entry: unlock the flash, run once */
  uint32_t uninit;         /**< entry: lock the flash again */
  uint32_t eraseChip;      /**< entry: mass erase */
  uint32_t eraseSector;    /**< entry: erase the sector containing a0 */
  uint32_t programPage;    /**< entry: program a0 from the buffer at a2 */
  uint32_t breakpoint;     /**< ra: the ebreak the routine returns to */
  uint32_t stackPointer;   /**< sp: top of the algorithm's stack */
  uint32_t programBuffer;  /**< staging area in target RAM */
  uint32_t algorithm;      /**< where the blob is copied to */
  uint32_t algorithmWords; /**< blob size in words */
  const uint32_t *blob;    /**< the blob itself */
  uint32_t bufferWords;    /**< staging area size in words */
};

namespace QkSwdFlashDetail
{

  /** CH32V003 flash controller, option bytes and the reset request register. */
  constexpr uint32_t FlashKeyr = 0x40022004U;
  constexpr uint32_t FlashOptKeyr = 0x40022008U;
  constexpr uint32_t FlashStatus = 0x4002200CU;
  constexpr uint32_t FlashCtrl = 0x40022010U;
  constexpr uint32_t FlashAddress = 0x40022014U;
  constexpr uint32_t FlashOptionByte = 0x4002201CU;
  constexpr uint32_t FlashModeKeyr = 0x40022024U;
  constexpr uint32_t FlashOptionByteData = 0x1FFFF800U;
  constexpr uint32_t ResetControl = 0xE000E048U;

  constexpr uint32_t Key1 = 0x45670123U;
  constexpr uint32_t Key2 = 0xCDEF89ABU;

  constexpr uint32_t CtrlPageProgram = 0x00010000U;
  constexpr uint32_t CtrlPageErase = 0x00020000U;
  constexpr uint32_t CtrlBufferLoad = 0x00040000U;
  constexpr uint32_t CtrlBufferReset = 0x00080000U;
  constexpr uint32_t CtrlStart = 0x00000040U;
  constexpr uint32_t CtrlOptionProgram = 1U << 4;
  constexpr uint32_t CtrlOptionErase = 1U << 5;
  constexpr uint32_t CtrlOptionLock = 1U << 9;

  constexpr uint32_t StatusBusy = 1U << 0;
  constexpr uint32_t StatusEndOfOperation = 1U << 5;

  constexpr uint32_t ResetKey = 0xBEEF0000U;
  constexpr uint32_t ResetRequest = 1U << 7;
  constexpr uint32_t OptionByteResetValue = 0x5AA5U;

  /** dcsr value that turns the routine's trailing ebreak into a debug entry. */
  constexpr uint16_t CsrDcsr = 0x07B0U;
  constexpr uint16_t CsrDpc = 0x07B1U;
  constexpr uint32_t DcsrEbreakToDebug = 0x00009603U;

  constexpr uint32_t FlashPollAttempts = 1000;
  constexpr uint32_t PollIntervalNs = 1000000U;
  constexpr uint8_t AlgorithmAttempts = 20;
  constexpr uint8_t ProtectAttempts = 5;
  constexpr uint8_t ArgumentCount = 4;

  /**
   * The reference programmer's algorithm, packed from the byte array in
   * openwch/ch32v003 components/flash_prg_algorithm.c (432 bytes).
   */
  constexpr uint32_t Algorithm[] = {
      0x00009002U,
      0x400227b7U,
      0x456706b7U,
      0x0007a023U,
      0x12368693U,
      0xcdef9737U,
      0x0713c3d4U,
      0xc3d89ab7U,
      0xd3d8d3d4U,
      0x80824501U,
      0x40022737U,
      0x45014b1cU,
      0x0807e793U,
      0x8082cb1cU,
      0x400227b7U,
      0x66ad4b98U,
      0x40003637U,
      0x00476713U,
      0x4b98cb98U,
      0xaaa68693U,
      0x04076713U,
      0x47d8cb98U,
      0xe7118b05U,
      0x45014b98U,
      0xcb989b6dU,
      0xc2148082U,
      0x27b7b7fdU,
      0x4b984002U,
      0x000206b7U,
      0x40003637U,
      0xcb988f55U,
      0x4b98cbc8U,
      0x869366adU,
      0x6713aaa6U,
      0xcb980407U,
      0x8b0547d8U,
      0x4b98e719U,
      0x77134501U,
      0xcb98fbf7U,
      0xc2148082U,
      0x1141b7f5U,
      0xc402c22aU,
      0x03f58593U,
      0x8199c602U,
      0x2737c62eU,
      0x4b1c4002U,
      0x053766c1U,
      0x8fd50008U,
      0xcb1c66adU,
      0x400035b7U,
      0x400227b7U,
      0xaaa68693U,
      0x8f494b98U,
      0x47d8cb98U,
      0xe73d8b05U,
      0x82b24712U,
      0x00040337U,
      0x4741c43aU,
      0x472281d8U,
      0x0002a283U,
      0x20230611U,
      0x4b980057U,
      0x00676733U,
      0x47d8cb98U,
      0xe7298b05U,
      0x82b24722U,
      0xc43a0711U,
      0x177d8198U,
      0x0ff77713U,
      0xfb7181d8U,
      0xcbd84712U,
      0x67134b98U,
      0xcb980407U,
      0x8b0547d8U,
      0x47d8e705U,
      0xc31d8b41U,
      0x76c147d8U,
      0x671316fdU,
      0xc7d80107U,
      0x45054b98U,
      0xcb988f75U,
      0x80820141U,
      0xb771c194U,
      0xbf45c194U,
      0xbfc9c194U,
      0x07134712U,
      0xc23a0407U,
      0x177d4732U,
      0xf73dc63aU,
      0x76c14b98U,
      0x8f7516fdU,
      0x4501cb98U,
      0x1151bfd1U,
      0xc202c02aU,
      0xc402058dU,
      0xc42e8189U,
      0x47924218U,
      0x00460693U,
      0x078a4602U,
      0x439c97b2U,
      0x00f71d63U,
      0x86364792U,
      0xc23e0785U,
      0x47a24712U,
      0xfef760e3U,
      0x01314501U,
      0x45058082U,
      0x0000bfedU,
  };

  /** The RAM layout that blob was linked for. */
  constexpr QkSwdFlashTarget ReferenceTarget = {
      0x20000004U,                              // init
      0x20000028U,                              // uninit
      0x20000038U,                              // eraseChip
      0x2000006aU,                              // eraseSector
      0x200000a2U,                              // programPage
      0x20000000U,                              // breakpoint (ebreak)
      0x20000800U,                              // stackPointer
      0x20000200U,                              // programBuffer
      0x20000000U,                              // algorithm
      sizeof(Algorithm) / sizeof(Algorithm[0]), // algorithmWords
      Algorithm,                                // blob
      1024U / 4U,                               // bufferWords
  };

} // namespace QkSwdFlashDetail

/**
 * Flash programming on top of the debug module.
 *
 * Port, QkSwd<Port> and QkSwdWire<Port> are the same object, so this class is
 * just one more layer: frames, hart control and now flash all live on it.
 * Nothing here is hardware specific in the QkSwd sense; the register addresses
 * in QkSwdFlashDetail are CH32V003 constants.
 */
template <typename Port>
class QkSwdFlash : public QkSwd<Port>
{
public:
  QkSwdFlash() = default;

  /** Hand a configured port in; it is copied into the base subobject. */
  explicit QkSwdFlash(Port port) : QkSwd<Port>(static_cast<Port &&>(port)) {}

  /** Replace the algorithm and its RAM layout. */
  void setTarget(const QkSwdFlashTarget &target) { target_ = target; }
  const QkSwdFlashTarget &target() const { return target_; }

  /* Route 1: RAM algorithm. The hart has to be halted before any of these. */

  /** Copy the blob into target RAM and run its init entry. */
  QkSwdStatus loadAlgorithm();

  /** Mass erase. Seconds on this part. */
  QkSwdStatus eraseChip();

  /** Erase the sector that contains address. */
  QkSwdStatus eraseSector(uint32_t address);

  /** Program wordCount words, staged through the algorithm's buffer. */
  QkSwdStatus program(uint32_t address, const uint32_t *words, uint32_t wordCount);

  /** Run the uninit entry: lock the controller again. */
  QkSwdStatus lockFlash();

  /* Route 2: the controller registers, one page at a time. */

  /** Write both unlock key pairs, including the fast mode one. */
  QkSwdStatus unlockFastMode();

  /** Discard whatever the page buffer holds. */
  QkSwdStatus resetPageBuffer();

  /** Stage one word for the page currently being built. */
  QkSwdStatus loadPageBuffer(uint32_t address, uint32_t value);

  /** Erase the page that contains address. */
  QkSwdStatus erasePage(uint32_t address);

  /** Program the page that contains address from the staged buffer. */
  QkSwdStatus writePage(uint32_t address);

  /* Option bytes and reset. */

  /** Read the option byte register (bit 0 set means read protected). */
  QkSwdStatus readOptionByte(uint32_t &value);

  /** Erase the option byte page; leaves the protection bits erased. */
  QkSwdStatus eraseOptionBytePage();

  /**
   * Clear read protection. Erases the option page and writes the reset value
   * back, which takes twenty odd round trips; a reset has to follow before the
   * protection state changes.
   */
  QkSwdStatus removeReadProtect();

  /** Reset the target through its own reset register (NVIC keys). */
  QkSwdStatus requestSystemReset();

private:
  /**
   * Call a routine in target RAM: four arguments, a private stack, a return
   * address on an ebreak, and a0 as the result.
   */
  QkSwdStatus syscall(uint32_t entry, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3);

  QkSwdStatus waitFlashIdle();
  QkSwdStatus setFlashCtrl(uint32_t bits);
  QkSwdStatus clearFlashCtrl(uint32_t bits);

  QkSwdFlashTarget target_ = QkSwdFlashDetail::ReferenceTarget;
};

#include "qk_swd_flash_impl.h"

#endif // QK_SWD_FLASH_H