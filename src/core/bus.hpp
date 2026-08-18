// The data-side bus: DMEM plus the MMIO window.
//
// Instruction fetch does NOT go through here. The hart fetches from its own
// IMEM object, which is what makes this machine genuinely Harvard rather than
// von Neumann with two names for one memory.
#pragma once

#include <array>

#include "core/devices.hpp"
#include "core/memory.hpp"
#include "isa/types.hpp"

namespace rv::core {

class Bus {
public:
    explicit Bus(u32 dmem_size);

    Memory& dmem() { return dmem_; }
    const Memory& dmem() const { return dmem_; }

    UartDevice& uart() { return uart_; }
    const UartDevice& uart() const { return uart_; }
    LedDevice& leds() { return leds_; }
    const LedDevice& leds() const { return leds_; }
    SwitchDevice& switches() { return switches_; }
    const SwitchDevice& switches() const { return switches_; }
    CycleDevice& cycles() { return cycles_; }

    static bool is_mmio(Addr addr) { return addr >= kMmioBase && addr - kMmioBase < kMmioSize; }

    MemResult load(Addr addr, u8 width, bool is_signed);
    MemResult store(Addr addr, u8 width, u32 value);

    /// Non-trapping read for the memory viewer and the reverse-step delta.
    /// MMIO is deliberately *not* consulted: peeking at a device in a UI redraw
    /// must not trigger its side effects.
    u32 peek_word(Addr addr) const;
    void poke_word(Addr addr, u32 value);

    void reset();

private:
    Device* device_for(Addr addr) const;

    Memory dmem_;
    UartDevice uart_;
    LedDevice leds_;
    SwitchDevice switches_;
    CycleDevice cycles_;
    std::array<Device*, kMmioSlots> slots_{};
};

}  // namespace rv::core
