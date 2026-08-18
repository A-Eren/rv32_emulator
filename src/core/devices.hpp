// Memory-mapped devices.
//
// The map lives at 0xFFFF0000..0xFFFF00FF, sixteen slots of sixteen bytes,
// dispatched by (addr >> 4) & 0xF so there is no search on the store path.
// See docs/memory-map.md for the register-level contract.
#pragma once

#include <cstddef>
#include <string>

#include "core/trap.hpp"
#include "isa/types.hpp"

namespace rv::core {

inline constexpr Addr kMmioBase = 0xffff'0000u;
inline constexpr u32 kMmioSize = 0x100u;
inline constexpr std::size_t kMmioSlots = 16;

class Device {
public:
    virtual ~Device() = default;
    virtual const char* name() const = 0;
    /// Reads are non-const because a device read may have side effects.
    virtual u32 read(u32 offset, u8 width) = 0;
    virtual void write(u32 offset, u8 width, u32 value) = 0;
    virtual void reset() = 0;
};

/// Slot 0 (0xFFFF0000): a write-only character sink.
///   +0x0  W  transmit one byte
///   +0x4  R  status; bit 0 = transmitter ready, always 1 here
class UartDevice final : public Device {
public:
    const char* name() const override { return "uart"; }
    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { output_.clear(); }

    const std::string& output() const { return output_; }
    /// Reverse-step undo: a byte already shown cannot be un-sent to a real
    /// device, but for a buffered console we can drop it again.
    void truncate(std::size_t length);

private:
    std::string output_;
};

/// Slot 1 (0xFFFF0010): a 32-bit output register standing in for board LEDs.
class LedDevice final : public Device {
public:
    const char* name() const override { return "leds"; }
    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { value_ = 0; }

    u32 value() const { return value_; }
    void set_raw(u32 value) { value_ = value; }

private:
    u32 value_ = 0;
};

/// Slot 2 (0xFFFF0020): a 32-bit input register standing in for board switches.
/// Writes are ignored; the value is set from the UI.
class SwitchDevice final : public Device {
public:
    const char* name() const override { return "switches"; }
    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override {}

    u32 value() const { return value_; }
    void set_value(u32 value) { value_ = value; }

private:
    u32 value_ = 0;
};

/// Slot 3 (0xFFFF0030): the hart's cycle counter, mirrored here so that a
/// program can read it with a plain `lw` and not only through a CSR. It is
/// also `mtime`: this machine has no wall clock, and tying time to the cycle
/// counter is what keeps interrupts deterministic -- without that, reverse
/// stepping and the self-checking test programs would both stop working.
class CycleDevice final : public Device {
public:
    const char* name() const override { return "cycles"; }
    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { cycles_ = 0; }

    void set_cycles(u64 cycles) { cycles_ = cycles; }

private:
    u64 cycles_ = 0;
};

/// Slot 4 (0xFFFF0040): the timer compare register.
///
/// A machine timer interrupt is pending while `mtime >= mtimecmp`. It resets
/// disarmed (all ones), so a program that never touches it never sees one.
///
/// Deliberately 32 bits, where a real machine has a 64-bit mtimecmp that must
/// be written high half first to avoid a spurious interrupt in between. That
/// hazard is worth knowing about but not worth inflicting: mtime here is the
/// cycle counter, and no teaching program comes close to four billion cycles.
class TimerDevice final : public Device {
public:
    const char* name() const override { return "timer"; }
    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { compare_ = kDisarmed; }

    u32 compare() const { return compare_; }
    void set_compare(u32 value) { compare_ = value; }
    bool armed() const { return compare_ != kDisarmed; }
    bool pending(u64 now) const { return armed() && now >= compare_; }

    static constexpr u32 kDisarmed = ~u32{0};

private:
    u32 compare_ = kDisarmed;
};

/// Slot 5 (0xFFFF0050): software and external interrupt requests.
///
/// A real board would raise the external line from a button or a peripheral;
/// here it is a register so a program can raise its own and a person can poke
/// it from the debugger.
class IrqDevice final : public Device {
public:
    const char* name() const override { return "irq"; }
    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { requests_ = 0; }

    /// The mip bits this device is asserting.
    u32 requests() const { return requests_; }
    void set_requests(u32 value) { requests_ = value & (kIrqSoftware | kIrqExternal); }

private:
    u32 requests_ = 0;
};

/// Everything a single step can change in a device, captured so that reverse
/// stepping can put it back. Kept as one small POD rather than a field per
/// device, so adding a device does not mean touching the delta record.
struct DeviceState {
    u32 leds = 0;
    u32 timer_compare = TimerDevice::kDisarmed;
    u32 irq_requests = 0;
    std::size_t uart_length = 0;
};

}  // namespace rv::core
