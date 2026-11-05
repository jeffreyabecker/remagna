/**
  ******************************************************************************
  * @file    qk_swd_impl.h
  * @brief   Implementation of qkSwd, the debug module layer.
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

namespace qkSwdDetail
{

/* CTRL bits.  Bit 0 and 1 are the halt and resume request of the RISC-V debug
 * spec. The two high bits are undocumented WCH extras: the target wants them
 * around a reset (arm / release) and ignores them otherwise. */
constexpr uint32_t CtrlHaltRequest = 1U << 0;
constexpr uint32_t CtrlResumeRequest = 1U << 1;
constexpr uint32_t CtrlRelease = 1U << 28;
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

} // namespace qkSwdDetail

inline const char *toString(qkSwdStatus status)
{
  switch (status)
  {
  case qkSwdStatus::Ok:
    return "ok";
  case qkSwdStatus::Argument:
    return "argument";
  case qkSwdStatus::NoResponse:
    return "no response";
  case qkSwdStatus::Timeout:
    return "timeout";
  case qkSwdStatus::AbstractFault:
    return "abstract fault";
  }
  return "unknown";
}

/* ------------------------------------------------------------------------- */
/* PROGBUF selection                                                         */
/* ------------------------------------------------------------------------- */

template <typename Port>
const uint32_t *qkSwd<Port>::readProgram(qkSwdWidth width)
{
  switch (width)
  {
  case qkSwdWidth::Byte:
    return qkSwdDetail::Read8;
  case qkSwdWidth::Half:
    return qkSwdDetail::Read16;
  case qkSwdWidth::Word:
  default:
    return qkSwdDetail::Read32;
  }
}

template <typename Port>
const uint32_t *qkSwd<Port>::writeProgram(qkSwdWidth width)
{
  switch (width)
  {
  case qkSwdWidth::Byte:
    return qkSwdDetail::Write8;
  case qkSwdWidth::Half:
    return qkSwdDetail::Write16;
  case qkSwdWidth::Word:
  default:
    return qkSwdDetail::Write32;
  }
}

/* ------------------------------------------------------------------------- */
/* Bring up and hart control                                                 */
/* ------------------------------------------------------------------------- */

template <typename Port>
qkSwdStatus qkSwd<Port>::probe()
{
  this->reset();

  /* Until these two registers have been written the target keeps its output
   * driver off, the line stays released and every read looks like all ones.
   * 0x5AA5 is the write key, 0x0400 enables the debug output. */
  write(qkSwdRegister::RegCfgShadow, qkSwdDetail::ConfigKey);
  write(qkSwdRegister::RegCfgMask, qkSwdDetail::ConfigKey);

  /* The capability register is the only fixed value on the wire, so it is what
   * tells "a target answered" apart from "the line floated high". */
  if (read(qkSwdRegister::RegCapStatus) != qkSwdDetail::CapabilitySignature)
  {
    return qkSwdStatus::NoResponse;
  }
  return qkSwdStatus::Ok;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::halt()
{
  /* Halt request plus the arm bit, twice: the first write starts the request,
   * the second one settles it. The third write drops the arm bit and leaves the
   * hart stopped with the halt request latched. */
  const uint32_t request = qkSwdDetail::CtrlArm | qkSwdDetail::CtrlHaltRequest;
  write(qkSwdRegister::RegCtrl, request);
  write(qkSwdRegister::RegCtrl, request);

  const qkSwdStatus status = pollDmStatus(qkSwdDetail::DmHalted, qkSwdDetail::DmHalted);
  if (status != qkSwdStatus::Ok)
  {
    return status;
  }

  write(qkSwdRegister::RegCtrl, qkSwdDetail::CtrlHaltRequest);
  return qkSwdStatus::Ok;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::resume()
{
  const uint32_t request = qkSwdDetail::CtrlArm | qkSwdDetail::CtrlHaltRequest;
  write(qkSwdRegister::RegCtrl, request);
  write(qkSwdRegister::RegCtrl, request);
  write(qkSwdRegister::RegCtrl, qkSwdDetail::CtrlHaltRequest);

  /* Clearing halt and requesting resume in one write, then wait for the running
   * acknowledge: the hart is only really out of the debug module when both
   * status bits say so. */
  write(qkSwdRegister::RegCtrl,
                qkSwdDetail::CtrlHaltRequest | qkSwdDetail::CtrlResumeRequest);
  return pollDmStatus(qkSwdDetail::DmRunning, qkSwdDetail::DmRunning);
}

template <typename Port>
qkSwdStatus qkSwd<Port>::coreReset()
{
  /* Assert the reset, wait until the debug module reports it, then release and
   * wait until the acknowledge clears. The hart runs afterwards. */
  write(qkSwdRegister::RegCtrl,
                qkSwdDetail::CtrlHaltRequest | qkSwdDetail::CtrlResumeRequest);

  qkSwdStatus status = pollDmStatus(qkSwdDetail::DmReset, qkSwdDetail::DmReset);
  if (status != qkSwdStatus::Ok)
  {
    return status;
  }

  write(qkSwdRegister::RegCtrl, qkSwdDetail::CtrlHaltRequest);
  write(qkSwdRegister::RegCtrl,
                qkSwdDetail::CtrlRelease | qkSwdDetail::CtrlHaltRequest);
  return pollDmStatus(qkSwdDetail::DmReset, 0U);
}

template <typename Port>
qkSwdStatus qkSwd<Port>::dmReset()
{
  /* The debug module resets itself, so nothing is expected to answer while it
   * is away; what is checked here is that it came back. A reset debug module
   * needs probe() again before it accepts register traffic. */
  const uint32_t request = qkSwdDetail::CtrlHaltRequest | qkSwdDetail::CtrlResumeRequest;
  write(qkSwdRegister::RegCtrl, request);
  if (read(qkSwdRegister::RegCtrl) != request)
  {
    return qkSwdStatus::NoResponse;
  }

  write(qkSwdRegister::RegCtrl, qkSwdDetail::CtrlResumeRequest);
  if ((read(qkSwdRegister::RegCtrl) & qkSwdDetail::CtrlResumeRequest) != 0U)
  {
    return qkSwdStatus::Timeout;
  }
  return qkSwdStatus::Ok;
}


/* ------------------------------------------------------------------------- */
/* Debug module helpers                                                      */
/* ------------------------------------------------------------------------- */

template <typename Port>
qkSwdStatus qkSwd<Port>::pollDmStatus(uint32_t mask, uint32_t value)
{
  uint32_t status = qkSwdDetail::DmBusy;

  for (uint16_t attempt = 0; attempt < qkSwdDetail::DmPollAttempts; ++attempt)
  {
    status = read(qkSwdRegister::RegDmStatus);

    /* All ones means nothing is answering; keep trying, the target may still be
     * busy rather than absent. */
    if ((status != qkSwdDetail::DmBusy) && ((status & mask) == value))
    {
      return qkSwdStatus::Ok;
    }
    this->pauseNs(qkSwdDetail::PollIntervalNs);
  }
  return qkSwdStatus::Timeout;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::pollAbstract()
{
  for (uint16_t attempt = 0; attempt < qkSwdDetail::AbstractPollAttempts; ++attempt)
  {
    const uint32_t status = read(qkSwdRegister::RegAbstractCs);

    if ((status & qkSwdDetail::AbstractBusy) == 0U)
    {
      if ((status & qkSwdDetail::AbstractError) != 0U)
      {
        /* The exception code is sticky. Clearing it keeps the next command from
         * looking broken for the same reason. */
        write(qkSwdRegister::RegAbstractCs, qkSwdDetail::AbstractError);
        return qkSwdStatus::AbstractFault;
      }
      return qkSwdStatus::Ok;
    }
    this->pauseNs(qkSwdDetail::PollIntervalNs);
  }
  return qkSwdStatus::Timeout;
}

template <typename Port>
void qkSwd<Port>::loadProgBuf(const uint32_t *program)
{
  for (uint8_t word = 0; word < qkSwdDetail::ProgBufWords; ++word)
  {
    write(static_cast<qkSwdRegister>(static_cast<uint8_t>(qkSwdRegister::RegProgBuf0) + word),
                  program[word]);
  }
}

template <typename Port>
qkSwdStatus qkSwd<Port>::executeProgBuf()
{
  /* cmdtype 0, transfer clear, postexec set: run PROGBUF on the halted hart. */
  write(qkSwdRegister::RegCommand, qkSwdDetail::CmdPostExec);
  return pollAbstract();
}

/* ------------------------------------------------------------------------- */
/* Abstract commands                                                         */
/* ------------------------------------------------------------------------- */

template <typename Port>
qkSwdStatus qkSwd<Port>::readGpr(uint8_t index, uint32_t &value)
{
  if (index > 31U)
  {
    return qkSwdStatus::Argument;
  }

  write(qkSwdRegister::RegCommand,
                qkSwdDetail::CmdSize32 | qkSwdDetail::CmdTransfer | index);
  const qkSwdStatus status = pollAbstract();
  if (status != qkSwdStatus::Ok)
  {
    return status;
  }

  value = read(qkSwdRegister::RegData0);
  return qkSwdStatus::Ok;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::writeGpr(uint8_t index, uint32_t value)
{
  if (index > 31U)
  {
    return qkSwdStatus::Argument;
  }

  /* The value always travels through DATA0 before the command that consumes it:
   * the command write is what triggers the transfer. */
  write(qkSwdRegister::RegData0, value);
  write(qkSwdRegister::RegCommand,
                qkSwdDetail::CmdSize32 | qkSwdDetail::CmdTransfer | qkSwdDetail::CmdWrite | index);
  return pollAbstract();
}

template <typename Port>
qkSwdStatus qkSwd<Port>::readCsr(uint16_t csr, uint32_t &value)
{
  if (csr > 0x0FFFU)
  {
    return qkSwdStatus::Argument;
  }

  write(qkSwdRegister::RegCommand,
                qkSwdDetail::CmdSize32 | qkSwdDetail::CmdTransfer | csr);
  const qkSwdStatus status = pollAbstract();
  if (status != qkSwdStatus::Ok)
  {
    return status;
  }

  value = read(qkSwdRegister::RegData0);
  return qkSwdStatus::Ok;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::writeCsr(uint16_t csr, uint32_t value)
{
  if (csr > 0x0FFFU)
  {
    return qkSwdStatus::Argument;
  }

  write(qkSwdRegister::RegData0, value);
  write(qkSwdRegister::RegCommand,
                qkSwdDetail::CmdSize32 | qkSwdDetail::CmdTransfer | qkSwdDetail::CmdWrite | csr);
  return pollAbstract();
}

/* ------------------------------------------------------------------------- */
/* Memory access through PROGBUF                                             */
/* ------------------------------------------------------------------------- */

template <typename Port>
qkSwdStatus qkSwd<Port>::readMemoryOnce(uint32_t address, uint32_t &value, qkSwdWidth width)
{
  write(qkSwdRegister::RegData1, address); // the address goes in
  loadProgBuf(readProgram(width));
  const qkSwdStatus status = executeProgBuf();
  if (status != qkSwdStatus::Ok)
  {
    return status;
  }

  value = read(qkSwdRegister::RegData0); // the value comes back here
  return qkSwdStatus::Ok;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::writeMemoryOnce(uint32_t address, uint32_t value, qkSwdWidth width)
{
  write(qkSwdRegister::RegData0, value);
  write(qkSwdRegister::RegData1, address);
  loadProgBuf(writeProgram(width));
  return executeProgBuf();
}

template <typename Port>
qkSwdStatus qkSwd<Port>::readMemory(uint32_t address, uint32_t &value, qkSwdWidth width)
{
  /* A failed access is usually the hart tripping on a protected or absent
   * address, which is permanent, but a target that was still waking up looks
   * the same. Retrying a read is free, so it is worth it. */
  qkSwdStatus status = qkSwdStatus::Ok;
  for (uint8_t attempt = 0; attempt < qkSwdDetail::TransferAttempts; ++attempt)
  {
    status = readMemoryOnce(address, value, width);
    if (status == qkSwdStatus::Ok)
    {
      break;
    }
  }
  return status;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::writeMemory(uint32_t address, uint32_t value, qkSwdWidth width)
{
  qkSwdStatus status = qkSwdStatus::Ok;
  for (uint8_t attempt = 0; attempt < qkSwdDetail::TransferAttempts; ++attempt)
  {
    status = writeMemoryOnce(address, value, width);
    if (status == qkSwdStatus::Ok)
    {
      break;
    }
  }
  return status;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::readMemoryBlock(uint32_t address, uint32_t *buffer, uint16_t &words)
{
  if ((buffer == nullptr) || (words == 0U))
  {
    return qkSwdStatus::Argument;
  }

  const uint16_t requested = words;
  if (requested == 1U)
  {
    return readMemoryOnce(address, buffer[0], qkSwdWidth::Word);
  }

  /* Auto increment is armed before the command runs, so from here on every
   * DATA0 access re-runs the burst program and advances its own pointer. */
  write(qkSwdRegister::RegData1, address);
  write(qkSwdRegister::RegAutoExec, qkSwdDetail::AutoIncrement);
  loadProgBuf(qkSwdDetail::Block);

  qkSwdStatus status = executeProgBuf();
  if (status != qkSwdStatus::Ok)
  {
    write(qkSwdRegister::RegAutoExec, 0U);
    words = 0U;
    return status;
  }

  /* One ordinary frame, then one continue frame per further word, then the last
   * word with auto increment already off: the burst program bumps DATA1 before
   * it finishes, so the final value would otherwise be missed. */
  buffer[0] = read(qkSwdRegister::RegData0);
  for (uint16_t index = 1U; index < static_cast<uint16_t>(requested - 1U); ++index)
  {
    buffer[index] = this->readContinue();
  }

  write(qkSwdRegister::RegAutoExec, 0U);
  status = pollAbstract();
  if (status != qkSwdStatus::Ok)
  {
    words = 1U;
    return status;
  }

  buffer[requested - 1U] = read(qkSwdRegister::RegData0);
  words = requested;
  return qkSwdStatus::Ok;
}

template <typename Port>
qkSwdStatus qkSwd<Port>::writeMemoryBlock(uint32_t address, const uint32_t *buffer,
                                          uint16_t &words)
{
  if ((buffer == nullptr) || (words == 0U))
  {
    return qkSwdStatus::Argument;
  }

  const uint16_t requested = words;
  if (requested == 1U)
  {
    return writeMemoryOnce(address, buffer[0], qkSwdWidth::Word);
  }

  /* The first word is staged in DATA0 with a normal frame, before the burst
   * program exists; bit 0 of the address tells it "store, not load". */
  write(qkSwdRegister::RegData0, buffer[0]);
  write(qkSwdRegister::RegData1, address | 0x01U);
  loadProgBuf(qkSwdDetail::Block);

  const qkSwdStatus status = executeProgBuf();
  if (status != qkSwdStatus::Ok)
  {
    words = 0U;
    return status;
  }

  write(qkSwdRegister::RegAutoExec, qkSwdDetail::AutoIncrement);
  for (uint16_t index = 1U; index < requested; ++index)
  {
    this->writeContinue(buffer[index]);
  }
  write(qkSwdRegister::RegAutoExec, 0U);

  words = requested;
  return qkSwdStatus::Ok;
}

#endif // QK_SWD_IMPL_H