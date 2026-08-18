// The data-side bus: DMEM plus a configurable set of devices.
//
// Instruction fetch does NOT go through here. The hart fetches from its own
// IMEM object, which is what makes this machine genuinely Harvard rather than
// von Neumann with two names for one memory.
//
// Devices are attached to slots at runtime, so a machine can be given the
// peripherals an exercise needs and dropped ones it does not.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/device.hpp"
#include "core/devices.hpp"
#include "core/memory.hpp"
#include "isa/types.hpp"

namespace rv::core {

class Bus {
public:
    explicit Bus(u32 dmem_size);

    Memory& dmem() { return dmem_; }
    const Memory& dmem() const { return dmem_; }

    // ---- devices -----------------------------------------------------------

    /// Attach a device, taking the slots it needs. Returns nullptr if the slot
    /// is out of range or any of the slots it wants is occupied.
    Device* attach(std::size_t slot, std::unique_ptr<Device> device);
    /// Remove whatever occupies `slot`, including the slots it spans.
    bool detach(std::size_t slot);
    void detach_all();

    Device* device_at_slot(std::size_t slot) const;
    Device* device_for_address(Addr addr) const;
    /// Every attached device, each appearing once however many slots it spans.
    std::vector<Device*> devices() const;

    /// Find the first attached device of a given type, for the handful of
    /// places that need one specifically -- the hart updating mtime, or a test
    /// arming the timer. Returns nullptr when the machine has no such device,
    /// which is a legitimate configuration.
    template <typename T>
    T* find() const {
        for (Device* device : devices()) {
            if (auto* typed = dynamic_cast<T*>(device)) return typed;
        }
        return nullptr;
    }

    /// Attach the devices a machine has unless told otherwise.
    void attach_default_devices();

    // ---- access ------------------------------------------------------------

    static bool is_mmio(Addr addr) { return addr >= kMmioBase && addr - kMmioBase < kMmioSize; }

    MemResult load(Addr addr, u8 width, bool is_signed);
    MemResult store(Addr addr, u8 width, u32 value);

    /// Non-trapping, side-effect-free access, for the memory view, for editing
    /// by hand, and for the reverse-step delta. Devices participate through
    /// their own peek/poke, which is what lets a single one-word delta undo a
    /// write to a device with no per-device history.
    u32 peek_word(Addr addr) const;
    void poke_word(Addr addr, u32 value);

    /// The mip value the devices are asserting. Computed rather than stored,
    /// because the timer bit is a pure function of the cycle counter -- which
    /// is what makes interrupts reproducible under reverse stepping.
    u32 pending_interrupts(u64 now) const;

    /// The device a step wrote to, or npos. The history needs this to know
    /// whose aux state to capture.
    static constexpr std::size_t kNoSlot = ~std::size_t{0};

    void reset();

private:
    Memory dmem_;
    std::vector<std::unique_ptr<Device>> owned_;
    std::array<Device*, kMmioSlots> slots_{};
};

}  // namespace rv::core
