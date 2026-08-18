#include "core/bus.hpp"

namespace rv::core {

Bus::Bus(u32 dmem_size) : dmem_(0, dmem_size) {
    slots_[0] = &uart_;
    slots_[1] = &leds_;
    slots_[2] = &switches_;
    slots_[3] = &cycles_;
    slots_[4] = &timer_;
    slots_[5] = &irq_;
}

Device* Bus::device_for(Addr addr) const {
    const std::size_t slot = ((addr - kMmioBase) >> 4) & 0xfu;
    return slots_[slot];
}

MemResult Bus::load(Addr addr, u8 width, bool is_signed) {
    if (!is_mmio(addr)) return dmem_.read(addr, width, is_signed);

    if ((addr & (width - 1u)) != 0) return MemResult::failure(TrapCause::LoadAddressMisaligned);
    Device* device = device_for(addr);
    if (device == nullptr) return MemResult::failure(TrapCause::LoadAccessFault);
    return MemResult::success(device->read(addr & 0xfu, width));
}

MemResult Bus::store(Addr addr, u8 width, u32 value) {
    if (!is_mmio(addr)) return dmem_.write(addr, width, value);

    if ((addr & (width - 1u)) != 0) return MemResult::failure(TrapCause::StoreAddressMisaligned);
    Device* device = device_for(addr);
    if (device == nullptr) return MemResult::failure(TrapCause::StoreAccessFault);
    device->write(addr & 0xfu, width, value);
    return MemResult::success(0);
}

u32 Bus::peek_word(Addr addr) const { return is_mmio(addr) ? 0 : dmem_.read_word_raw(addr); }

void Bus::poke_word(Addr addr, u32 value) {
    if (!is_mmio(addr)) dmem_.write_word_raw(addr, value);
}

u32 Bus::pending_interrupts(u64 now) const {
    u32 pending = irq_.requests();
    if (timer_.pending(now)) pending |= kIrqTimer;
    return pending;
}

DeviceState Bus::capture() const {
    DeviceState state;
    state.leds = leds_.value();
    state.timer_compare = timer_.compare();
    state.irq_requests = irq_.requests();
    state.uart_length = uart_.output().size();
    return state;
}

void Bus::restore(const DeviceState& state) {
    leds_.set_raw(state.leds);
    timer_.set_compare(state.timer_compare);
    irq_.set_requests(state.irq_requests);
    // A byte already sent to real hardware could not be recalled, but a
    // buffered console can drop it again.
    uart_.truncate(state.uart_length);
}

void Bus::reset() {
    dmem_.clear();
    uart_.reset();
    leds_.reset();
    cycles_.reset();
    timer_.reset();
    irq_.reset();
    // Switch inputs deliberately survive a reset: they model physical switches
    // on a board, which do not move when the core is reset.
}

}  // namespace rv::core
