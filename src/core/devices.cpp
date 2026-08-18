#include "core/devices.hpp"

namespace rv::core {

u32 UartDevice::read(u32 offset, u8 /*width*/) {
    if (offset == 0x4) return 1;  // transmitter always ready
    return 0;
}

void UartDevice::write(u32 offset, u8 /*width*/, u32 value) {
    if (offset != 0x0) return;
    output_.push_back(static_cast<char>(value & 0xffu));
}

void UartDevice::truncate(std::size_t length) {
    if (length < output_.size()) output_.resize(length);
}

u32 LedDevice::read(u32 offset, u8 /*width*/) { return offset == 0 ? value_ : 0; }

void LedDevice::write(u32 offset, u8 /*width*/, u32 value) {
    if (offset == 0) value_ = value;
}

u32 SwitchDevice::read(u32 offset, u8 /*width*/) { return offset == 0 ? value_ : 0; }

void SwitchDevice::write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) {
    // Input-only: a store here is silently dropped, matching a real read-only
    // port. The assembler warns about it at compile time instead.
}

u32 CycleDevice::read(u32 offset, u8 /*width*/) {
    if (offset == 0x0) return static_cast<u32>(cycles_);
    if (offset == 0x4) return static_cast<u32>(cycles_ >> 32);
    return 0;
}

void CycleDevice::write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) {}

}  // namespace rv::core
