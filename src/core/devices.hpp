// The devices that can be attached to the bus.
//
// Register layouts are in docs/memory-map.md. Offsets here are relative to
// whichever slot a device is attached to, so nothing hard-codes an address.
#pragma once

#include <string>
#include <vector>

#include "core/device.hpp"
#include "core/trap.hpp"

namespace rv::core {

/// A character sink.
///   +0x0  W  transmit one byte
///   +0x4  R  status; bit 0 = transmitter ready, always 1
class UartDevice final : public Device {
public:
    std::string_view type_name() const override { return "uart"; }
    std::string_view description() const override { return "character output"; }

    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { output_.clear(); }

    u32 peek(u32 offset) const override { return offset == 0x4 ? 1u : 0u; }
    // The output is a growing string, which no peek/poke pair can express, so
    // its length rides in the aux slot instead.
    u32 aux_state() const override { return static_cast<u32>(output_.size()); }
    void set_aux_state(u32 value) override { truncate(value); }

    std::string state_summary() const override;

    const std::string& output() const { return output_; }
    void truncate(std::size_t length);

private:
    std::string output_;
};

/// A 32-bit output register standing in for board LEDs.
class LedDevice final : public Device {
public:
    std::string_view type_name() const override { return "leds"; }
    std::string_view description() const override { return "32-bit output register"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { value_ = 0; }

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) value_ = value;
    }

    std::string state_summary() const override;

    u32 value() const { return value_; }
    void set_raw(u32 value) { value_ = value; }

private:
    u32 value_ = 0;
};

/// A row of switches the user flips.
class SwitchDevice final : public Device {
public:
    std::string_view type_name() const override { return "switches"; }
    std::string_view description() const override { return "32-bit input register"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) override {}  // input only
    void reset() override {}  // physical switches do not move when a core resets

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    u32 value() const { return value_; }
    void set_value(u32 value) { value_ = value; }

private:
    u32 value_ = 0;
};

/// A momentary button. Reads as 1 while held and can raise the external
/// interrupt line -- the simplest thing that behaves like asynchronous input.
///   +0x0  RW  1 while pressed; writing 0 acknowledges
class ButtonDevice final : public Device {
public:
    std::string_view type_name() const override { return "button"; }
    std::string_view description() const override { return "momentary input, can raise IRQ"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { pressed_ = false; }

    u32 peek(u32 offset) const override { return offset == 0 ? (pressed_ ? 1u : 0u) : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) pressed_ = (value & 1u) != 0;
    }

    u32 interrupt_lines(u64 /*now*/) const override {
        return (pressed_ && raises_interrupt_) ? kIrqExternal : 0u;
    }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    void set_raises_interrupt(bool raises) { raises_interrupt_ = raises; }
    bool raises_interrupt() const { return raises_interrupt_; }

private:
    bool pressed_ = false;
    bool raises_interrupt_ = true;
};

/// A number the user types in, standing in for a sensor or an ADC.
class ValueDevice final : public Device {
public:
    std::string_view type_name() const override { return "value"; }
    std::string_view description() const override { return "numeric input register"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) override {}
    void reset() override {}

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    u32 value() const { return value_; }
    void set_value(u32 value) { value_ = value; }

private:
    u32 value_ = 0;
};

/// The cycle counter, mirrored into the address space so a program can read it
/// with a plain `lw`. This is also `mtime`: the machine has no wall clock, and
/// tying time to the cycle counter is what keeps interrupts reproducible.
///   +0x0  R  low word    +0x4  R  high word
class CycleDevice final : public Device {
public:
    std::string_view type_name() const override { return "mtime"; }
    std::string_view description() const override { return "cycle counter / mtime"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) override {}
    void reset() override { cycles_ = 0; }

    u32 peek(u32 offset) const override;
    std::string state_summary() const override;

    void set_cycles(u64 cycles) { cycles_ = cycles; }

private:
    u64 cycles_ = 0;
};

/// The timer compare register. A machine timer interrupt is pending while
/// `mtime >= mtimecmp`. Resets disarmed, so a program that never touches it
/// never sees one.
class TimerDevice final : public Device {
public:
    std::string_view type_name() const override { return "mtimecmp"; }
    std::string_view description() const override { return "timer compare"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { compare_ = kDisarmed; }

    u32 peek(u32 offset) const override { return offset == 0 ? compare_ : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) compare_ = value;
    }

    u32 interrupt_lines(u64 now) const override {
        return (armed() && now >= compare_) ? kIrqTimer : 0u;
    }
    std::string state_summary() const override;

    u32 compare() const { return compare_; }
    void set_compare(u32 value) { compare_ = value; }
    bool armed() const { return compare_ != kDisarmed; }

    static constexpr u32 kDisarmed = ~u32{0};

private:
    u32 compare_ = kDisarmed;
};

/// Software and external interrupt requests a program can raise itself.
///   +0x0  RW  software request, bit 0
///   +0x4  RW  external request, bit 0
class IrqDevice final : public Device {
public:
    std::string_view type_name() const override { return "irq"; }
    std::string_view description() const override { return "software/external IRQ requests"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { requests_ = 0; }

    u32 peek(u32 offset) const override;
    void poke(u32 offset, u32 value) override;

    u32 interrupt_lines(u64 /*now*/) const override { return requests_; }
    std::string state_summary() const override;

    u32 requests() const { return requests_; }
    void set_requests(u32 value) { requests_ = value & (kIrqSoftware | kIrqExternal); }

private:
    u32 requests_ = 0;
};

/// A block of memory in the device address space: extra RAM, or a ROM holding
/// a lookup table. Occupies as many slots as its size needs.
///
/// This is what makes "give me some ready-made memory" a peripheral rather than
/// a special case. It can be preloaded from a .mem file, marked read-only, and
/// edited by hand from the memory view like anything else -- and because writes
/// to it go through the ordinary peek/poke path, reverse stepping covers them
/// with no extra machinery.
class RamDevice final : public Device {
public:
    explicit RamDevice(u32 size_bytes, bool read_only = false);

    std::string_view type_name() const override { return read_only_ ? "rom" : "ram"; }
    std::string_view description() const override {
        return read_only_ ? "read-only memory block" : "memory block";
    }
    std::size_t slot_count() const override;

    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override;

    u32 peek(u32 offset) const override;
    void poke(u32 offset, u32 value) override;
    std::string state_summary() const override;

    u32 size() const { return static_cast<u32>(bytes_.size()); }
    bool read_only() const { return read_only_; }

    /// Fill from a word image and keep it as the reset contents, so resetting
    /// the machine restores a preloaded table rather than zeroing it.
    void load_words(const std::vector<Word>& words);

private:
    std::vector<u8> bytes_;
    std::vector<u8> initial_;
    bool read_only_ = false;
};

/// A bank of 7-segment digits: one byte per digit, shown as glyphs rather than
/// as hex.
///   +0x0  RW  digits 0..3, one byte each (0x0-0xf, anything else blanks it)
class DisplayDevice final : public Device {
public:
    explicit DisplayDevice(int digits = 4);

    std::string_view type_name() const override { return "display"; }
    std::string_view description() const override { return "7-segment digits"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { value_ = kBlank; }

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) value_ = value;
    }

    std::string state_summary() const override;
    /// The digits as text, for the interface to draw.
    std::string rendered() const;

    int digits() const { return digits_; }

private:
    static constexpr u32 kBlank = 0xffff'ffffu;
    int digits_ = 4;
    u32 value_ = kBlank;
};

}  // namespace rv::core
