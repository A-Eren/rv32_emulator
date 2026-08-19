#include "core/devices.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace rv::core {
namespace {

std::string summary(const char* format, ...) {
    char buffer[192];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return buffer;
}

std::string bit_string(u32 value, char one, char zero) {
    std::string out;
    for (int index = 31; index >= 0; --index) {
        out.push_back(((value >> index) & 1u) != 0 ? one : zero);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

u32 UartDevice::read(u32 offset, u8 /*width*/) {
    return offset == 0x4 ? 1u : 0u;  // transmitter always ready
}

void UartDevice::write(u32 offset, u8 /*width*/, u32 value) {
    if (offset != 0x0) return;
    output_.push_back(static_cast<char>(value & 0xffu));
}

void UartDevice::truncate(std::size_t length) {
    if (length < output_.size()) output_.resize(length);
}

std::string UartDevice::state_summary() const {
    return summary("%zu bytes written", output_.size());
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

std::string LedDevice::state_summary() const {
    return summary("%08x  ", value_) + bit_string(value_, '#', '.');
}

// ---------------------------------------------------------------------------
// Switches
// ---------------------------------------------------------------------------

std::vector<InputControl> SwitchDevice::inputs() const {
    return {InputControl{"switches", InputKind::Toggle, value_, 32, 0, 0xffff'ffffu}};
}

void SwitchDevice::set_input(std::size_t index, u32 value) {
    if (index == 0) value_ = value;
}

std::string SwitchDevice::state_summary() const {
    return summary("%08x  ", value_) + bit_string(value_, '1', '0');
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

std::vector<InputControl> ButtonDevice::inputs() const {
    return {InputControl{"press", InputKind::Button, pressed_ ? 1u : 0u, 1, 0, 1},
            InputControl{"raises IRQ", InputKind::Toggle, raises_interrupt_ ? 1u : 0u, 1, 0, 1}};
}

void ButtonDevice::set_input(std::size_t index, u32 value) {
    if (index == 0) pressed_ = (value & 1u) != 0;
    if (index == 1) raises_interrupt_ = (value & 1u) != 0;
}

std::string ButtonDevice::state_summary() const {
    return summary("%s%s", pressed_ ? "pressed" : "released",
                   raises_interrupt_ ? ", raises external IRQ" : "");
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

std::vector<InputControl> ValueDevice::inputs() const {
    return {InputControl{"value", InputKind::Number, value_, 32, 0, 0xffff'ffffu}};
}

void ValueDevice::set_input(std::size_t index, u32 value) {
    if (index == 0) value_ = value;
}

std::string ValueDevice::state_summary() const {
    return summary("%u  (0x%08x)", value_, value_);
}

// ---------------------------------------------------------------------------
// Cycle counter
// ---------------------------------------------------------------------------

u32 CycleDevice::peek(u32 offset) const {
    if (offset == 0x0) return static_cast<u32>(cycles_);
    if (offset == 0x4) return static_cast<u32>(cycles_ >> 32);
    return 0;
}

std::string CycleDevice::state_summary() const {
    return summary("%llu cycles", static_cast<unsigned long long>(cycles_));
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

std::string TimerDevice::state_summary() const {
    return armed() ? summary("fires at cycle %u", compare_) : std::string("disarmed");
}

// ---------------------------------------------------------------------------
// Interrupt requests
// ---------------------------------------------------------------------------

u32 IrqDevice::peek(u32 offset) const {
    if (offset == 0x0) return (requests_ & kIrqSoftware) != 0 ? 1u : 0u;
    if (offset == 0x4) return (requests_ & kIrqExternal) != 0 ? 1u : 0u;
    return 0;
}

void IrqDevice::poke(u32 offset, u32 value) {
    // Only bit 0 of each register is meaningful; writing 0 clears the request,
    // which is how a handler acknowledges it.
    if (offset == 0x0) {
        requests_ = (requests_ & ~kIrqSoftware) | ((value & 1u) != 0 ? kIrqSoftware : 0u);
    } else if (offset == 0x4) {
        requests_ = (requests_ & ~kIrqExternal) | ((value & 1u) != 0 ? kIrqExternal : 0u);
    }
}

std::string IrqDevice::state_summary() const {
    return summary("software=%d external=%d", (requests_ & kIrqSoftware) != 0 ? 1 : 0,
                   (requests_ & kIrqExternal) != 0 ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Memory block
// ---------------------------------------------------------------------------

RamDevice::RamDevice(u32 size_bytes, bool read_only)
    : bytes_((size_bytes + 3u) & ~3u, 0), read_only_(read_only) {
    initial_ = bytes_;
}

std::size_t RamDevice::slot_count() const {
    return std::max<std::size_t>(1, (bytes_.size() + kSlotSize - 1) / kSlotSize);
}

u32 RamDevice::read(u32 offset, u8 width) {
    if (offset + width > bytes_.size()) return 0;
    u32 value = 0;
    for (u8 index = 0; index < width; ++index) {
        value |= static_cast<u32>(bytes_[offset + index]) << (8 * index);  // little-endian
    }
    return value;
}

void RamDevice::write(u32 offset, u8 width, u32 value) {
    // A ROM ignores stores rather than trapping: that is what the hardware
    // does. Catching the mistake belongs in the assembler, not here.
    if (read_only_ || offset + width > bytes_.size()) return;
    for (u8 index = 0; index < width; ++index) {
        bytes_[offset + index] = static_cast<u8>(value >> (8 * index));
    }
}

u32 RamDevice::peek(u32 offset) const {
    const u32 aligned = offset & ~3u;
    if (aligned + 4 > bytes_.size()) return 0;
    return static_cast<u32>(bytes_[aligned]) | (static_cast<u32>(bytes_[aligned + 1]) << 8) |
           (static_cast<u32>(bytes_[aligned + 2]) << 16) |
           (static_cast<u32>(bytes_[aligned + 3]) << 24);
}

void RamDevice::poke(u32 offset, u32 value) {
    // Deliberately ignores read_only_: this is the debugger's path, and being
    // unable to edit a ROM from a memory view would be an obstacle rather than
    // a safeguard.
    const u32 aligned = offset & ~3u;
    if (aligned + 4 > bytes_.size()) return;
    for (u8 index = 0; index < 4; ++index) {
        bytes_[aligned + index] = static_cast<u8>(value >> (8 * index));
    }
}

void RamDevice::reset() { bytes_ = initial_; }

void RamDevice::load_words(const std::vector<Word>& words) {
    const std::size_t count = std::min(words.size(), bytes_.size() / 4);
    for (std::size_t index = 0; index < count; ++index) {
        poke(static_cast<u32>(index * 4), words[index]);
    }
    // Keep it as the reset image, so a preloaded table survives a reset.
    initial_ = bytes_;
}

std::string RamDevice::state_summary() const {
    return summary("%u bytes%s", static_cast<u32>(bytes_.size()),
                   read_only_ ? ", read-only" : "");
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

DisplayDevice::DisplayDevice(int digits) : digits_(std::clamp(digits, 1, 4)) {}

std::string DisplayDevice::rendered() const {
    std::string out;
    // Most significant digit first, which is how a number is read.
    for (int index = digits_ - 1; index >= 0; --index) {
        const u32 nibble = (value_ >> (8 * index)) & 0xffu;
        out.push_back(nibble <= 0xf ? "0123456789abcdef"[nibble] : '-');
    }
    return out;
}

std::string DisplayDevice::state_summary() const { return summary("[%s]", rendered().c_str()); }

}  // namespace rv::core
