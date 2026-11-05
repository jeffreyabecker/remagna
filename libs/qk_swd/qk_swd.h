/**
  ******************************************************************************
  * @file    qk_swd.h
  * @brief   The WCH single wire debug protocol at the debug module level.
  *
  * QkSwdWire (see qk_swd_wire.h) takes care of what a 0, a 1 and a frame look
  * like on the wire. This class is the layer above it: what the 32 bit payload
  * of a frame means.
  *
  * DEBUG MODULE REGISTERS (7 bit addresses, see QkSwdRegister)
  * ----------------------------------------------------------
  *   0x04 DATA0       data in / out of the abstract command engine
  *   0x05 DATA1       second data register, used for the address
  *   0x10 CTRL        halt / resume / reset requests
  *   0x11 DMSTATUS    halted, running and reset acknowledge bits
  *   0x12 HARTINFO    hart capabilities, written once after reset
  *   0x16 ABSTRACTCS  abstract command status: busy + sticky error code
  *   0x17 COMMAND     the abstract command itself
  *   0x18 ABSTRACTAUTO auto increment for block transfers
  *   0x20..0x27 PROGBUF0..7 eight words of RISC-V code the hart runs
  *   0x40 HARTSUM0    hart summary / interesting state
  *   0x7C CAPR status capability register, read during bring up
  *   0x7D/0x7E        configuration mask and shadow, also bring up only
  *
  * None of these are memory mapped registers of the target: every one of them
  * is addressed by the address byte of a frame, so a "register read" costs one
  * frame and a "register write" costs one frame.
  *
  * Usage is: a port line that derives from QkSwdWire<PortLine>, then QkSwd on
  * top of it. The stack is one object - pin, frames and debug module - because
  * each layer derives from the one below instead of holding a reference to it.
  *
  *     struct MyLine : QkSwdWire<MyLine>
  *     {
  *       void driveLow();  void driveHigh();  void release();
  *       bool sample();    void waitNs(uint32_t ns);
  *     };
  *
  *     QkSwd<MyLine> debugger;
  *     if (debugger.probe() != QkSwdStatus::Ok) { ... }
  *     debugger.halt();
  *
  * probe() runs once after power up; the abstract command helpers (readGpr,
  * readCsr, readMemory, ...) only work while the hart is halted, halt() puts it
  * there and resume() lets it run again.
  ******************************************************************************
  */

#ifndef QK_SWD_H
#define QK_SWD_H

#include <stdint.h>

#include "qk_swd_wire.h"

/** Result of an operation that involves the target answering something. */
enum class QkSwdStatus : uint8_t
{
  Ok = 0,
  Argument,      /**< the caller asked for something a frame cannot carry */
  NoResponse,    /**< no debug module answered the capability read */
  Timeout,       /**< the debug module never settled a status bit */
  AbstractFault, /**< the hart reported an exception for the abstract command */
  AlgorithmFault, /**< code the debugger ran on the target reported a failure */
};

/** Access width of the memory helpers, matching the PROGBUF variants. */
enum class QkSwdWidth : uint8_t
{
  Byte = 0,
  Half = 1,
  Word = 2,
};

/**
  * Debug module register addresses, 7 bit. The wire adds the direction bit, so
  * RegDmStatus becomes 0x22 for a write and 0x22 for a read of DMSTATUS.
  */
enum class QkSwdRegister : uint8_t
{
  RegData0 = 0x04,
  RegData1 = 0x05,
  RegCtrl = 0x10,
  RegDmStatus = 0x11,
  RegHartInfo = 0x12,
  RegAbstractCs = 0x16,
  RegCommand = 0x17,
  RegAutoExec = 0x18,
  RegProgBuf0 = 0x20,
  RegProgBuf7 = 0x27,
  RegHartSum0 = 0x40,
  RegCapStatus = 0x7C,
  RegCfgMask = 0x7D,
  RegCfgShadow = 0x7E,
};

/** Text form of a status, for logs. */
const char *toString(QkSwdStatus status);

/**
  * Debug module layer, mixed into the port by CRTP.
  *
  * Port derives from QkSwdWire<Port> and supplies the five line calls; deriving
  * from Port here puts write(), read(), readContinue(), reset() and the bit
  * helpers one inheritance hop away, already inlined. There is no wire handle to
  * store, hand around or keep alive.
  */
template <typename Port>
class QkSwd : public Port
{
public:
  QkSwd() = default;

  /** Hand a configured port in; it is copied into the base subobject. */
  explicit QkSwd(Port port) : Port(static_cast<Port &&>(port)) {}

  /**
    * Raw frames. The register enum is accepted directly, and the byte taking
    * overloads stay visible for addresses the enum does not name.
    *
    * A write is fire and forget - the wire has no acknowledge - and a read
    * returns whatever was on the line, all ones when nobody answers.
    */
  using Port::read;
  using Port::write;

  /** Write one debug module register. */
  void write(QkSwdRegister reg, uint32_t value)
  {
    Port::write(static_cast<uint8_t>(reg), value);
  }

  /** Read one debug module register. */
  uint32_t read(QkSwdRegister reg) { return Port::read(static_cast<uint8_t>(reg)); }

  /**
    * Forces the target out of whatever frame it was in, enables its output
    * driver and checks that a debug module answers. Run once after power up.
    */
  QkSwdStatus probe();

  /** Halt the hart and leave it stopped. */
  QkSwdStatus halt();

  /** Let the halted hart run again. */
  QkSwdStatus resume();

  /**
    * Wait until the hart has stopped on its own (an ebreak, a breakpoint, a
    * fault). This is the other half of resume(): the flash syscall protocol
    * relies on the target code ending in ebreak and reporting through a0.
    */
  QkSwdStatus waitHalted();

  /** Pulse the hart reset and leave the hart running. */
  QkSwdStatus coreReset();

  /** Reset the debug module itself, taking the DebugModuleControl with it. */
  QkSwdStatus dmReset();

  /* Abstract commands. These drive the hart, so they need a halted target. */

  QkSwdStatus readGpr(uint8_t index, uint32_t &value);
  QkSwdStatus writeGpr(uint8_t index, uint32_t value);
  QkSwdStatus readCsr(uint16_t csr, uint32_t &value);
  QkSwdStatus writeCsr(uint16_t csr, uint32_t value);

  /**
    * Read / write memory through the hart with a small PROGBUF program.
    *
    * A single access is two commands worth of frames: the address goes into
    * DATA1, the RISC-V load or store is copied into PROGBUF, COMMAND starts it,
    * ABSTRACTCS says when it finished and DATA0 carries the value back.
    */
  QkSwdStatus readMemory(uint32_t address, uint32_t &value, QkSwdWidth width = QkSwdWidth::Word);
  QkSwdStatus writeMemory(uint32_t address, uint32_t value, QkSwdWidth width = QkSwdWidth::Word);

  /**
    * Burst variants. words is in/out and reports how much was actually moved,
    * which matters when the target stops answering half way through. Both need
    * the auto increment bit in ABSTRACTAUTO and use continue frames.
    */
  QkSwdStatus readMemoryBlock(uint32_t address, uint32_t *buffer, uint16_t &words);
  QkSwdStatus writeMemoryBlock(uint32_t address, const uint32_t *buffer, uint16_t &words);

private:
  static const uint32_t *readProgram(QkSwdWidth width);
  static const uint32_t *writeProgram(QkSwdWidth width);

  QkSwdStatus pollDmStatus(uint32_t mask, uint32_t value);
  QkSwdStatus pollAbstract();
  void loadProgBuf(const uint32_t *program);
  QkSwdStatus executeProgBuf();
  QkSwdStatus readMemoryOnce(uint32_t address, uint32_t &value, QkSwdWidth width);
  QkSwdStatus writeMemoryOnce(uint32_t address, uint32_t value, QkSwdWidth width);
};

/* The implementation follows; it is a separate file only to keep the class
 * declaration readable, everything is still header only and inlines away. */
#include "qk_swd_impl.h"

#endif // QK_SWD_H