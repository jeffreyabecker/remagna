/**
  ******************************************************************************
  * @file    qk_swd_impl.h
  * @brief   Implementation of QkSwd, the debug module layer.
  *
  * Included by qk_swd.h after the class declaration. Everything here is a
  * template over the port type, so it has to live in a header; the separate
  * file only exists to keep the declaration readable.
  *
  * The only target architecture specific part is the RISC-V code in the PROGBUF
  * blobs, because that code is executed by the target hart, not by the host.
  ******************************************************************************
  */

#ifndef QK_SWD_IMPL_H
#define QK_SWD_IMPL_H

#include "qk_swd.h"

namespace QkSwdDetail
{

/* CTRL bits.  Bit 0 and 1 are the halt and resume request of the RISC-V debug
 * spec. The two high bits are undocumented WCH extras: the target wants them
 * around a reset (arm / release) and ignores them otherwise. */
constexpr uint32_t CtrlHaltRequest = 1U << 0;
constexpr uint32_t CtrlResumeRequest = 1U << 1;
constexpr uint32_t CtrlRelease = 1U << 28;
constexpr uint32_t CtrlRun = 1U << 30;
constexpr uint32_t CtrlArm = 1U << 31;

/* DMSTATUS groups: bits 9:8 halted, bits 11:10 running, bits 19:18 the reset
 * acknowledge. Two bits each because a group is "all" and "any" hart, and this
 * part has exactly one hart. */
constexpr uint32_t DmHalted = 0x3U << 8;
constexpr uint32_t DmRunning = 0x3U << 10;
constexpr uint32_t DmReset = 0x3U << 18;

/* ABSTRACTCS: busy in bit 12, the sticky exception code in bits 10:8. */
constexpr uint32_t AbstractBusy = 1U << 12;
constexpr uint32_t AbstractError = 0x7U << 8;

/* COMMAND, for the access register command type (cmdtype 0 in bits 31:24). */
constexpr uint32_t CmdSize32 = 2U << 20;
constexpr uint32_t CmdPostExec = 1U << 18;
constexpr uint32_t CmdTransfer = 1U << 17;
constexpr uint32_t CmdWrite = 1U << 16;

/* ABSTRACTAUTO bit 0: re-run the last command for every DATA0 access, which is
 * what turns a burst into back to back continue frames. */
constexpr uint32_t AutoIncrement = 1U << 0;

/* Bring up. The capability register answers with this fixed pattern; the two
 * configuration registers take the 0x5AA5 write key and one enable bit. */
constexpr uint32_t CapabilitySignature = 0x00010403U;
constexpr uint32_t ConfigKey = 0x5AA50400U;

/* Poll budgets. The reference programmer waits up to 200 ms for a status to
 * settle and retries an abstract command that failed. */
constexpr uint16_t DmPollAttempts = 200;
constexpr uint16_t AbstractPollAttempts = 200;
constexpr uint8_t TransferAttempts = 3;
constexpr uint32_t PollIntervalNs = 1000000U;

constexpr uint8_t ProgBufWords = 8;
constexpr uint32_t DmBusy = 0xFFFFFFFFU;

/*
 * The two memory access programs.
 *
 * There is no "read memory" command in the WCH debug module. What there is, is
 * eight words of RISC-V code in PROGBUF that the hart executes on request, so
 * memory access is hand assembled here: DATA1 carries the address in, DATA0
 * carries the value out or in, and dscratch plus CSR 0x7b3 are used as scratch
 * so the program does not disturb the hart's own registers.
 *
 *   csrw  dscratch, a0      save a0
 *   csrw  0x7b3, a1         save a1
 *   lui   a0, 0xe0000       a0 = debug module base
 *   lw    a1, 248(a0)       a1 = DATA1 (0xf8), the address to touch
 *   ...                     the load or store, 8/16/32 bit variant
 *   ...                     DATA0 (0xf4) write or read
 *   csrr  a0, dscratch      restore
 *   csrr  a1, 0x7b3         restore
 *   c.ebreak                return to the debug module
 *
 * The last word packs two compressed instructions, which is why the values look
 * nothing like the disassembly above.
 */

constexpr uint32_t Read8[ProgBufWords] = {0x7b251073U, 0x7b359073U, 0xe0000537U, 0x0f852583U,
                                          0x0005c583U, 0x0eb52a23U, 0x7b202573U, 0x7b3025f3U};
constexpr uint32_t Read16[ProgBufWords] = {0x7b251073U, 0x7b359073U, 0xe0000537U, 0x0f852583U,
                                           0x0005d583U, 0x0eb52a23U, 0x7b202573U, 0x7b3025f3U};
constexpr uint32_t Read32[ProgBufWords] = {0x7b251073U, 0x7b359073U, 0xe0000537U, 0x0f852583U,
                                           0x2a23418cU, 0x25730eb5U, 0x25f37b20U, 0x90027b30U};

constexpr uint32_t Write8[ProgBufWords] = {0x7b251073U, 0x7b359073U, 0xe0000537U, 0x0f852583U,
                                           0x0f452503U, 0x00a58023U, 0x7b202573U, 0x7b3025f3U};
constexpr uint32_t Write16[ProgBufWords] = {0x7b251073U, 0x7b359073U, 0xe0000537U, 0x0f852583U,
                                            0x0f452503U, 0x00a59023U, 0x7b202573U, 0x7b3025f3U};
constexpr uint32_t Write32[ProgBufWords] = {0x7b251073U, 0x7b359073U, 0xe0000537U, 0x0f852583U,
                                            0x0f452503U, 0x2573c188U, 0x25f37b20U, 0x90027b30U};

/*
 * The burst program. Same idea, but it derives the direction from bit 0 of the
 * address the master left in DATA1, does one word and then stores the
 * incremented address back into DATA1 so the next DATA0 access continues where
 * the previous one stopped. That is the whole secret of a block transfer: the
 * master keeps sending 32 bit frames and this program advances the pointer.
 *
 *   lui   a0, 0xe0000
 *   addi  a0, a0, 244       a0 = &DATA0
 *   lw    a1, 4(a0)         a1 = DATA1, bit 0 = direction
 *   andi  a3, a1, -4        a3 = word aligned address
 *   beq   a1, a3, read      direction bit clear -> read
 *   lw    a2, 0(a0)         a2 = DATA0
 *   sw    a2, 0(a3)         store to memory
 *   j     bump
 * read:
 *   lw    a2, 0(a3)
 *   sw    a2, 0(a0)         DATA0 = loaded word
 * bump:
 *   addi  a1, a1, 4
 *   sw    a1, 4(a0)         DATA1 = next address
 */
constexpr uint32_t Block[ProgBufWords] = {0xe0000537U, 0x0f450513U, 0xf693414cU, 0x8563ffc5U,
                                          0x411000d5U, 0xa019c290U, 0xc1104290U, 0xc14c0591U};

} // namespace QkSwdDetail

inline const char *toString(QkSwdStatus status)
{
  switch (status)
  {
  case QkSwdStatus::Ok:
    return "ok";
  case QkSwdStatus::Argument:
    return "argument";
  case QkSwdStatus::NoResponse:
    return "no response";
  case QkSwdStatus::Timeout:
    return "timeout";
  case QkSwdStatus::AbstractFault:
    return "abstract fault";
  case QkSwdStatus::AlgorithmFault:
    return "target code fault";
  }
  return "unknown";
}

/* ------------------------------------------------------------------------- */
/* PROGBUF selection                                                         */
/* ------------------------------------------------------------------------- */

template <typename Port>
const uint32_t *QkSwd<Port>::readProgram(QkSwdWidth width)
{
  switch (width)
  {
  case QkSwdWidth::Byte:
    return QkSwdDetail::Read8;
  case QkSwdWidth::Half:
    return QkSwdDetail::Read16;
  case QkSwdWidth::Word:
  default:
    return QkSwdDetail::Read32;
  }
}

template <typename Port>
const uint32_t *QkSwd<Port>::writeProgram(QkSwdWidth width)
{
  switch (width)
  {
  case QkSwdWidth::Byte:
    return QkSwdDetail::Write8;
  case QkSwdWidth::Half:
    return QkSwdDetail::Write16;
  case QkSwdWidth::Word:
  default:
    return QkSwdDetail::Write32;
  }
}

/* ------------------------------------------------------------------------- */
/* Bring up and hart control                                                 */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwd<Port>::probe()
{
  this->reset();

  /* Until these two registers have been written the target keeps its output
   * driver off, the line stays released and every read looks like all ones.
   * 0x5AA5 is the write key, 0x0400 enables the debug output. */
  write(QkSwdRegister::RegCfgShadow, QkSwdDetail::ConfigKey);
  write(QkSwdRegister::RegCfgMask, QkSwdDetail::ConfigKey);

  /* The capability register is the only fixed value on the wire, so it is what
   * tells "a target answered" apart from "the line floated high". */
  if (read(QkSwdRegister::RegCapStatus) != QkSwdDetail::CapabilitySignature)
  {
    return QkSwdStatus::NoResponse;
  }
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::halt()
{
  /* Halt request plus the arm bit, twice: the first write starts the request,
   * the second one settles it. The third write drops the arm bit and leaves the
   * hart stopped with the halt request latched. */
  const uint32_t request = QkSwdDetail::CtrlArm | QkSwdDetail::CtrlHaltRequest;
  write(QkSwdRegister::RegCtrl, request);
  write(QkSwdRegister::RegCtrl, request);

  const QkSwdStatus status = pollDmStatus(QkSwdDetail::DmHalted, QkSwdDetail::DmHalted);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  write(QkSwdRegister::RegCtrl, QkSwdDetail::CtrlHaltRequest);
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::resume()
{
  const uint32_t request = QkSwdDetail::CtrlArm | QkSwdDetail::CtrlHaltRequest;
  write(QkSwdRegister::RegCtrl, request);
  write(QkSwdRegister::RegCtrl, request);
  write(QkSwdRegister::RegCtrl, QkSwdDetail::CtrlHaltRequest);

  /* Run. 0x40000001 is what the reference programmer writes to leave the pause
   * mode: bit 30 plus a latched halt request, not the resumereq bit. The hart is
   * only really running when both running status bits say so - which is why a
   * hart that halts again immediately (an ebreak right at the entry point, as
   * in a flash syscall) can make this return Timeout. */
  write(QkSwdRegister::RegCtrl, QkSwdDetail::CtrlRun | QkSwdDetail::CtrlHaltRequest);
  return pollDmStatus(QkSwdDetail::DmRunning, QkSwdDetail::DmRunning);
}

template <typename Port>
QkSwdStatus QkSwd<Port>::waitHalted()
{
  return pollDmStatus(QkSwdDetail::DmHalted, QkSwdDetail::DmHalted);
}

template <typename Port>
QkSwdStatus QkSwd<Port>::coreReset()
{
  /* Assert the reset, wait until the debug module reports it, then release and
   * wait until the acknowledge clears. The hart runs afterwards. */
  write(QkSwdRegister::RegCtrl,
                QkSwdDetail::CtrlHaltRequest | QkSwdDetail::CtrlResumeRequest);

  QkSwdStatus status = pollDmStatus(QkSwdDetail::DmReset, QkSwdDetail::DmReset);
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  write(QkSwdRegister::RegCtrl, QkSwdDetail::CtrlHaltRequest);
  write(QkSwdRegister::RegCtrl,
                QkSwdDetail::CtrlRelease | QkSwdDetail::CtrlHaltRequest);
  return pollDmStatus(QkSwdDetail::DmReset, 0U);
}

template <typename Port>
QkSwdStatus QkSwd<Port>::dmReset()
{
  /* The debug module resets itself, so nothing is expected to answer while it
   * is away; what is checked here is that it came back. A reset debug module
   * needs probe() again before it accepts register traffic. */
  const uint32_t request = QkSwdDetail::CtrlHaltRequest | QkSwdDetail::CtrlResumeRequest;
  write(QkSwdRegister::RegCtrl, request);
  if (read(QkSwdRegister::RegCtrl) != request)
  {
    return QkSwdStatus::NoResponse;
  }

  write(QkSwdRegister::RegCtrl, QkSwdDetail::CtrlResumeRequest);
  if ((read(QkSwdRegister::RegCtrl) & QkSwdDetail::CtrlResumeRequest) != 0U)
  {
    return QkSwdStatus::Timeout;
  }
  return QkSwdStatus::Ok;
}


/* ------------------------------------------------------------------------- */
/* Debug module helpers                                                      */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwd<Port>::pollDmStatus(uint32_t mask, uint32_t value)
{
  uint32_t status = QkSwdDetail::DmBusy;

  for (uint16_t attempt = 0; attempt < QkSwdDetail::DmPollAttempts; ++attempt)
  {
    status = read(QkSwdRegister::RegDmStatus);

    /* All ones means nothing is answering; keep trying, the target may still be
     * busy rather than absent. */
    if ((status != QkSwdDetail::DmBusy) && ((status & mask) == value))
    {
      return QkSwdStatus::Ok;
    }
    this->pauseNs(QkSwdDetail::PollIntervalNs);
  }
  return QkSwdStatus::Timeout;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::pollAbstract()
{
  for (uint16_t attempt = 0; attempt < QkSwdDetail::AbstractPollAttempts; ++attempt)
  {
    const uint32_t status = read(QkSwdRegister::RegAbstractCs);

    if ((status & QkSwdDetail::AbstractBusy) == 0U)
    {
      if ((status & QkSwdDetail::AbstractError) != 0U)
      {
        /* The exception code is sticky. Clearing it keeps the next command from
         * looking broken for the same reason. */
        write(QkSwdRegister::RegAbstractCs, QkSwdDetail::AbstractError);
        return QkSwdStatus::AbstractFault;
      }
      return QkSwdStatus::Ok;
    }
    this->pauseNs(QkSwdDetail::PollIntervalNs);
  }
  return QkSwdStatus::Timeout;
}

template <typename Port>
void QkSwd<Port>::loadProgBuf(const uint32_t *program)
{
  for (uint8_t word = 0; word < QkSwdDetail::ProgBufWords; ++word)
  {
    write(static_cast<QkSwdRegister>(static_cast<uint8_t>(QkSwdRegister::RegProgBuf0) + word),
                  program[word]);
  }
}

template <typename Port>
QkSwdStatus QkSwd<Port>::executeProgBuf()
{
  /* cmdtype 0, transfer clear, postexec set: run PROGBUF on the halted hart. */
  write(QkSwdRegister::RegCommand, QkSwdDetail::CmdPostExec);
  return pollAbstract();
}

/* ------------------------------------------------------------------------- */
/* Abstract commands                                                         */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwd<Port>::readGpr(uint8_t index, uint32_t &value)
{
  if (index > 31U)
  {
    return QkSwdStatus::Argument;
  }

  write(QkSwdRegister::RegCommand,
                QkSwdDetail::CmdSize32 | QkSwdDetail::CmdTransfer | index);
  const QkSwdStatus status = pollAbstract();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  value = read(QkSwdRegister::RegData0);
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::writeGpr(uint8_t index, uint32_t value)
{
  if (index > 31U)
  {
    return QkSwdStatus::Argument;
  }

  /* The value always travels through DATA0 before the command that consumes it:
   * the command write is what triggers the transfer. */
  write(QkSwdRegister::RegData0, value);
  write(QkSwdRegister::RegCommand,
                QkSwdDetail::CmdSize32 | QkSwdDetail::CmdTransfer | QkSwdDetail::CmdWrite | index);
  return pollAbstract();
}

template <typename Port>
QkSwdStatus QkSwd<Port>::readCsr(uint16_t csr, uint32_t &value)
{
  if (csr > 0x0FFFU)
  {
    return QkSwdStatus::Argument;
  }

  write(QkSwdRegister::RegCommand,
                QkSwdDetail::CmdSize32 | QkSwdDetail::CmdTransfer | csr);
  const QkSwdStatus status = pollAbstract();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  value = read(QkSwdRegister::RegData0);
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::writeCsr(uint16_t csr, uint32_t value)
{
  if (csr > 0x0FFFU)
  {
    return QkSwdStatus::Argument;
  }

  write(QkSwdRegister::RegData0, value);
  write(QkSwdRegister::RegCommand,
                QkSwdDetail::CmdSize32 | QkSwdDetail::CmdTransfer | QkSwdDetail::CmdWrite | csr);
  return pollAbstract();
}

/* ------------------------------------------------------------------------- */
/* Memory access through PROGBUF                                             */
/* ------------------------------------------------------------------------- */

template <typename Port>
QkSwdStatus QkSwd<Port>::readMemoryOnce(uint32_t address, uint32_t &value, QkSwdWidth width)
{
  write(QkSwdRegister::RegData1, address); // the address goes in
  loadProgBuf(readProgram(width));
  const QkSwdStatus status = executeProgBuf();
  if (status != QkSwdStatus::Ok)
  {
    return status;
  }

  value = read(QkSwdRegister::RegData0); // the value comes back here
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::writeMemoryOnce(uint32_t address, uint32_t value, QkSwdWidth width)
{
  write(QkSwdRegister::RegData0, value);
  write(QkSwdRegister::RegData1, address);
  loadProgBuf(writeProgram(width));
  return executeProgBuf();
}

template <typename Port>
QkSwdStatus QkSwd<Port>::readMemory(uint32_t address, uint32_t &value, QkSwdWidth width)
{
  /* A failed access is usually the hart tripping on a protected or absent
   * address, which is permanent, but a target that was still waking up looks
   * the same. Retrying a read is free, so it is worth it. */
  QkSwdStatus status = QkSwdStatus::Ok;
  for (uint8_t attempt = 0; attempt < QkSwdDetail::TransferAttempts; ++attempt)
  {
    status = readMemoryOnce(address, value, width);
    if (status == QkSwdStatus::Ok)
    {
      break;
    }
  }
  return status;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::writeMemory(uint32_t address, uint32_t value, QkSwdWidth width)
{
  QkSwdStatus status = QkSwdStatus::Ok;
  for (uint8_t attempt = 0; attempt < QkSwdDetail::TransferAttempts; ++attempt)
  {
    status = writeMemoryOnce(address, value, width);
    if (status == QkSwdStatus::Ok)
    {
      break;
    }
  }
  return status;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::readMemoryBlock(uint32_t address, uint32_t *buffer, uint16_t &words)
{
  if ((buffer == nullptr) || (words == 0U))
  {
    return QkSwdStatus::Argument;
  }

  const uint16_t requested = words;
  if (requested == 1U)
  {
    return readMemoryOnce(address, buffer[0], QkSwdWidth::Word);
  }

  /* Auto increment is armed before the command runs, so from here on every
   * DATA0 access re-runs the burst program and advances its own pointer. */
  write(QkSwdRegister::RegData1, address);
  write(QkSwdRegister::RegAutoExec, QkSwdDetail::AutoIncrement);
  loadProgBuf(QkSwdDetail::Block);

  QkSwdStatus status = executeProgBuf();
  if (status != QkSwdStatus::Ok)
  {
    write(QkSwdRegister::RegAutoExec, 0U);
    words = 0U;
    return status;
  }

  /* One ordinary frame, then one continue frame per further word, then the last
   * word with auto increment already off: the burst program bumps DATA1 before
   * it finishes, so the final value would otherwise be missed. */
  buffer[0] = read(QkSwdRegister::RegData0);
  for (uint16_t index = 1U; index < static_cast<uint16_t>(requested - 1U); ++index)
  {
    buffer[index] = this->readContinue();
  }

  write(QkSwdRegister::RegAutoExec, 0U);
  status = pollAbstract();
  if (status != QkSwdStatus::Ok)
  {
    words = 1U;
    return status;
  }

  buffer[requested - 1U] = read(QkSwdRegister::RegData0);
  words = requested;
  return QkSwdStatus::Ok;
}

template <typename Port>
QkSwdStatus QkSwd<Port>::writeMemoryBlock(uint32_t address, const uint32_t *buffer,
                                          uint16_t &words)
{
  if ((buffer == nullptr) || (words == 0U))
  {
    return QkSwdStatus::Argument;
  }

  const uint16_t requested = words;
  if (requested == 1U)
  {
    return writeMemoryOnce(address, buffer[0], QkSwdWidth::Word);
  }

  /* The first word is staged in DATA0 with a normal frame, before the burst
   * program exists; bit 0 of the address tells it "store, not load". */
  write(QkSwdRegister::RegData0, buffer[0]);
  write(QkSwdRegister::RegData1, address | 0x01U);
  loadProgBuf(QkSwdDetail::Block);

  const QkSwdStatus status = executeProgBuf();
  if (status != QkSwdStatus::Ok)
  {
    words = 0U;
    return status;
  }

  write(QkSwdRegister::RegAutoExec, QkSwdDetail::AutoIncrement);
  for (uint16_t index = 1U; index < requested; ++index)
  {
    this->writeContinue(buffer[index]);
  }
  write(QkSwdRegister::RegAutoExec, 0U);

  words = requested;
  return QkSwdStatus::Ok;
}

#endif // QK_SWD_IMPL_H