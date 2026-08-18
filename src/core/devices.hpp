// Memory-mapped devices.
//
// The map lives at 0xFFFF0000..0xFFFF00FF, sixteen slots of sixteen bytes,
// dispatched by (addr >> 4) & 0xF so there is no search on the store path.
// See docs/memory-map.md for the register-level contract.
#pragma once

#include <string>

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
/// program can read it with a plain `lw` and not only through a CSR.
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

}  // namespace rv::core
