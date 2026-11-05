/**
 ******************************************************************************
 * @file    qk_swd_wire.h
 * @brief   The WCH single wire debug protocol at the wire level: bits, frames,
 *          timing. Knows nothing about debug modules.
 *
 * CH32V003 class parts have no SWD pins. Instead one half duplex line carries
 * everything a RISC-V debug module needs. There is no clock wire, so the master
 * has to produce every phase of every bit itself and the target recovers the
 * bits from the durations it sees. This file is therefore mostly a description
 * of a timing contract.
 *
 * The layering is:
 *
 *   QkSwdWire   the bit encoding, the register frames, the line reset: the
 *               CRTP base. Everything that happens between two edges.
 *   Port line   the five wire calls of one pin, mixed into the wire by
 *               deriving from QkSwdWire<PortLine>.
 *   QkSwd       what the frames mean: debug module registers, hart control,
 *               abstract commands, memory access (see qk_swd.h).
 *
 * THE LINE IS PART OF THE WIRE
 * ----------------------------
 * The protocol is templated on the port that drives it (CRTP): the port derives
 * from QkSwdWire<Port>, implements the five line calls, and inherits the frame
 * and bit machinery. There is no line object, no base interface and no member
 * to keep alive - the line *is* the wire, seen from the pin.
 *
 *     struct MyLine : QkSwdWire<MyLine>
 *     {
 *       void driveLow();           // pin low, push pull
 *       void driveHigh();          // pin high, push pull
 *       void release();            // pin input float
 *       bool sample();             // pin level
 *       void waitNs(uint32_t ns);  // never returns early
 *     };
 *
 *     MyLine line;              // line.write(), line.read(), line.reset() too
 *     QkSwd<MyLine> debugger;   // or QkSwd<MyLine> debugger(someLine);
 *
 * Each of the five is reached through one static_cast (QkSwdWire::lineDriveLow
 * and friends), so the hop is resolved at compile time and disappears. A frame
 * is 41 timed phases and an indirect call is a visible slice of a 250 ns
 * budget, spent inside the timed section, because the wait only starts once the
 * call has landed.
 *
 * The debug module layer follows the same rule: QkSwd<Port> derives from Port,
 * so the whole stack - pin, frames, debug module - is one object (see qk_swd.h).
 *
 * The trade is that the five names are only checked where they are used: a
 * missing one is a compile error inside QkSwdWire naming the call.
 *
 * WIRE FORMAT
 * -----------
 * A register frame is:
 *
 *     start bit | 8 bit address byte | 32 bit data | stop bit
 *
 *   start bit   a "1" shaped pulse (short low, short high). It announces an
 *               address phase.
 *   address     7 bit register address, MSB first, with the direction bit as
 *               the last bit: 0 = the target drives the data phase back,
 *               1 = the master writes the data phase.
 *   data        32 bits, MSB first. When the target drives a bit it holds the
 *               line low for the "0" time and lets it rise for the "1" time;
 *               the master samples shortly after its own low phase.
 *   stop bit    the master releases the line and leaves it high. The target
 *               uses that gap to finish whatever the frame asked for.
 *
 * Block transfers never repeat the address phase. After one ordinary frame the
 * target increments its own address pointer, and every further frame is:
 *
 *     continue start bit ("0" shaped pulse) | 32 bit data | stop bit
 *
 * The pulse shape is the discriminator: a "1" start bit means "an address
 * follows", a "0" start bit means "same register, next word".
 *
 * TIMING CONTRACT
 * ---------------
 * The protocol is defined in multiples of T = 125 ns, but real targets accept
 * a wide range. The reference CH32F103 programmer measured these phases on the
 * wire; the values are the defaults here ("medium", which is what a freshly
 * reset target expects, and also what the reference calls 2x mode):
 *
 *   phase            medium     fast      requirement
 *   1 bit low        250 ns     165 ns    short: it must stay under the
 *                                         target's 1/0 threshold, roughly 4T,
 *                                         or the target reads a 0
 *   1 bit high       250 ns     165 ns    the line must be back high before
 *                                         the next low phase starts
 *   0 bit low        750 ns     490 ns    this duration is what separates a 0
 *                                         from a 1, and it is the phase most
 *                                         sensitive to host jitter
 *   0 bit high       250 ns     165 ns
 *   read low         250 ns     165 ns    the master owns the line this long
 *   read sample      at the end of the read low phase
 *   read hold        250 ns     165 ns    extra wait when a 0 came back, so
 *                                         the target can finish its own low
 *                                         phase before the next bit
 *   stop             2.9 us     1.7 us    line released high, once per frame
 *   wake pulse       20 ms      20 ms     line held low after power up
 *
 * Three rules fall out of the table:
 *
 *   * every duration is a minimum. Slower is safe as long as the target has
 *     not fallen back to idle waiting for the next bit; faster than the
 *     "1" threshold silently turns 1 bits into 0 bits.
 *   * a frame must not be interrupted. The target has no way to resynchronise
 *     in the middle of a frame, so an interrupt that lands between the address
 *     and data phases corrupts the frame and the debug module answers
 *     garbage. A host that bit bangs from a timer interrupt, or that disables
 *     interrupts for the length of a frame, is doing the right thing.
 *   * there is no acknowledge. A write is fire and forget, a read always
 *     returns 32 bits. A dead or unpowered target shows up as all ones, so the
 *     only way to tell "answered" from "floating high" is a register with a
 *     value you already know (the capability register, see QkSwd::probe).
 ******************************************************************************
 */

#ifndef QK_SWD_WIRE_H
#define QK_SWD_WIRE_H

#include <stdint.h>

/**
 * Every duration the protocol needs, in nanoseconds.
 *
 * See the timing table in the file header for what each phase means and which
 * way it may be relaxed.
 */
struct QkSwdTiming
{
  uint32_t data1LowNs = 250;      /**< low phase of a 1 bit */
  uint32_t data1HighNs = 250;     /**< high phase that closes a 1 bit */
  uint32_t data0LowNs = 750;      /**< low phase of a 0 bit */
  uint32_t data0HighNs = 250;     /**< high phase that closes a 0 bit */
  uint32_t readLowNs = 250;       /**< low phase the master drives before it samples */
  uint32_t readHoldNs = 250;      /**< extra low time assumed after a 0 was read */
  uint32_t stopNs = 2900;         /**< line released high after the last data bit */
  uint32_t resetHighNs = 5000000; /**< idle high before the wake pulse */
  uint32_t resetLowNs = 20000000; /**< wake pulse: longer than any frame timeout */
  uint32_t settleNs = 5000000;    /**< idle high after the wake pulse */

  /** The reference programmer's 2x mode, the default a target expects. */
  static QkSwdTiming medium() { return QkSwdTiming{}; }

  /** The reference programmer's 1x mode; short wires and a fast host only. */
  static QkSwdTiming fast()
  {
    QkSwdTiming timing = medium();
    timing.data1LowNs = 165;
    timing.data1HighNs = 165;
    timing.data0LowNs = 490;
    timing.data0HighNs = 165;
    timing.readLowNs = 165;
    timing.readHoldNs = 165;
    timing.stopNs = 1700;

    /* The wake pulse and the settle gaps are timeouts of the target, not phases
     * of the protocol, so they keep their medium values. */
    return timing;
  }
};

namespace QkSwdDetail
{
  constexpr uint8_t DirectionWrite = 0x01U; /**< address byte bit 0: master writes */
  constexpr uint8_t AddressBits = 8;
  constexpr uint8_t DataBits = 32;
} // namespace QkSwdDetail

/**
 * Bit and frame level of the protocol, as a CRTP base for the port.
 *
 * Derived is the port: it inherits this class and supplies driveLow(),
 * driveHigh(), release(), sample() and waitNs(). Everything here runs on that
 * inheritance, so a derived object can send frames as well as drive its pin.
 */
template <typename Derived>
class QkSwdWire
{
public:
  /** Replaces every phase duration; see QkSwdTiming. */
  void setTiming(const QkSwdTiming &timing) { timing_ = timing; }
  const QkSwdTiming &timing() const { return timing_; }

  /** Gap filler for callers that have a non-frame pause to fill. */
  void pauseNs(uint32_t nanoseconds) { lineWaitNs(nanoseconds); }

  /* Frames. reg is the 7 bit register address; the direction bit is added. */

  /** Address frame, master drives the data phase. Fire and forget. */
  void write(uint8_t reg, uint32_t value)
  {
    sendStartBit(true);

    uint8_t address = static_cast<uint8_t>((reg << 1) | QkSwdDetail::DirectionWrite);
    for (uint8_t bit = 0; bit < QkSwdDetail::AddressBits; ++bit)
    {
      sendBit((address & 0x80U) != 0U);
      address = static_cast<uint8_t>(address << 1);
    }

    sendWordBits(value);
    sendStop();
  }

  /** Address frame, target drives the data phase. */
  uint32_t read(uint8_t reg)
  {
    sendStartBit(true);

    uint8_t address = static_cast<uint8_t>(reg << 1); // direction bit clear: a read
    for (uint8_t bit = 0; bit < QkSwdDetail::AddressBits; ++bit)
    {
      sendBit((address & 0x80U) != 0U);
      address = static_cast<uint8_t>(address << 1);
    }

    /* From here on the target drives the line, so the master only supplies the
     * low phase of each bit and listens at the end of it. */
    const uint32_t value = readBits();
    sendStop();
    return value;
  }

  /** Continue frame: same register, next word. Needs the target's auto increment. */
  void writeContinue(uint32_t value)
  {
    /* A 0 shaped start bit means "no address, keep the pointer": that is what
     * makes a burst cheaper than a register write per word. */
    sendStartBit(false);
    sendWordBits(value);
    sendStop();
  }

  uint32_t readContinue()
  {
    sendStartBit(false);
    const uint32_t value = readBits();
    sendStop();
    return value;
  }

  /** Wake pulse: the only way out of a frame the target is still stuck in. */
  void reset()
  {
    /* A target that is still in the middle of a frame from the previous session
     * cannot parse anything new, so force it back to idle: high, a pulse longer
     * than any of its timeouts, high again. 20 ms also covers a target that is
     * still busy inside a flash operation. */
    lineRelease();
    lineWaitNs(timing_.resetHighNs);
    lineDriveLow();
    lineWaitNs(timing_.resetLowNs);
    lineRelease();
    lineWaitNs(timing_.settleNs);
  }

  /* Bit level, for experiments, recorders and understanding. Frames above are
   * the normal entry point; these are the pieces they are built from. */

  /** One bit: a low phase for the bit value, then the fixed high phase. */
  void sendBit(bool one)
  {
    lineDriveLow();
    lineWaitNs(one ? timing_.data1LowNs : timing_.data0LowNs);
    lineDriveHigh();
    lineWaitNs(one ? timing_.data1HighNs : timing_.data0HighNs);
  }

  /** A start bit: a data bit whose shape announces the frame type. */
  void sendStartBit(bool one)
  {
    /* The start bit is an ordinary data bit as far as the wire is concerned; its
     * shape is what announces the frame type. */
    sendBit(one);
  }

  /** 32 bits, MSB first. */
  void sendWordBits(uint32_t value)
  {
    for (uint8_t bit = 0; bit < QkSwdDetail::DataBits; ++bit)
    {
      sendBit((value & 0x80000000U) != 0U);
      value <<= 1;
    }
  }

  /** 32 bits, MSB first, sampled from the target. */
  uint32_t readBits()
  {
    uint32_t value = 0;

    for (uint8_t bit = 0; bit < QkSwdDetail::DataBits; ++bit)
    {
      value <<= 1;

      /* The master always owns the beginning of a bit; the target only holds the
       * line down while it answers with a 0, and it counts on the master to give
       * it that low phase. */
      lineDriveLow();
      lineWaitNs(timing_.readLowNs);
      lineRelease();

      if (lineSample())
      {
        value |= 1U;
      }
      else
      {
        /* Still low: wait out the rest of the target's 0 phase so the next bit
         * is not driven against it. */
        lineWaitNs(timing_.readHoldNs);
      }
    }

    return value;
  }

  /** Release the line and hold it high for the stop period. */
  void sendStop()
  {
    /* Released, not driven high: the pull up does the work and there is no way
     * to fight the target if it is still finishing its last bit. */
    lineRelease();
    lineWaitNs(timing_.stopNs);
  }

protected:
  /* The CRTP hop. Each line call is one static_cast to the port, which the
   * compiler folds away; a port that forgot one of them fails here, by name. */
  void lineDriveLow() { static_cast<Derived *>(this)->driveLow(); }
  void lineDriveHigh() { static_cast<Derived *>(this)->driveHigh(); }
  void lineRelease() { static_cast<Derived *>(this)->release(); }
  bool lineSample() { return static_cast<Derived *>(this)->sample(); }
  void lineWaitNs(uint32_t nanoseconds) { static_cast<Derived *>(this)->waitNs(nanoseconds); }

private:
  QkSwdTiming timing_ = QkSwdTiming::medium();
};

#endif // QK_SWD_WIRE_H