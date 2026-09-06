#include "dbg/pipeline.hpp"

namespace rv::dbg {
namespace {

constexpr std::size_t index_of(Stage stage) { return static_cast<std::size_t>(stage); }

}  // namespace

const char* stage_name(Stage stage) {
    switch (stage) {
        case Stage::IF: return "IF";
        case Stage::ID: return "ID";
        case Stage::EX: return "EX";
        case Stage::MEM: return "MEM";
        case Stage::WB: return "WB";
    }
    return "??";
}

const char* stage_abbrev(Stage stage) {
    // Two columns each, so the space-time diagram is a grid rather than a
    // ragged edge. Only MEM has to give anything up.
    return stage == Stage::MEM ? "ME" : stage_name(stage);
}

Pipeline::Pipeline(DebugSession& session) : session_(&session) { reset(); }

// ---------------------------------------------------------------------------
// Filling and emptying
// ---------------------------------------------------------------------------

void Pipeline::start_over() {
    slots_ = {};
    history_.clear();
    ticks_ = 0;
    last_flushed_ = 0;
}

void Pipeline::reset() {
    start_over();
    fetch_pc_ = session_->cpu().pc;
    record(false, 0);
}

void Pipeline::resync() {
    start_over();

    // Put back what is knowable. The instruction that just ran goes where it
    // ran, and the next two go where they would have been fetched to. That
    // keeps the rule tick() relies on: the instruction arriving in EX is the
    // one at the pc.
    const core::StepOutcome& last = session_->last_step();
    if (last.instr.valid()) {
        Slot& ex = slots_[index_of(Stage::EX)];
        ex.valid = true;
        ex.pc = last.pc_before;
        ex.raw = last.raw;
        ex.instr = last.instr;
        ex.serial = next_serial_++;
        ex.committed = true;
    }

    fetch_pc_ = session_->cpu().pc;
    if (session_->has_program() && !session_->halted()) {
        for (const Stage stage : {Stage::ID, Stage::IF}) {
            Slot& slot = slots_[index_of(stage)];
            slot = fetch(fetch_pc_);
            fetch_pc_ += slot.instr.length;
        }
    }
    record(false, 0);
}

Slot Pipeline::fetch(Addr addr) {
    Slot slot;
    slot.valid = true;
    slot.pc = addr;
    slot.raw = session_->read_imem_instr(addr);
    slot.instr = isa::decode(slot.raw);
    slot.serial = next_serial_++;
    return slot;
}

void Pipeline::record(bool committed, u8 flushed) {
    CycleRecord entry;
    entry.tick = ticks_;
    entry.stages = slots_;
    entry.fetch_pc = fetch_pc_;
    entry.committed = committed;
    entry.flushed = flushed;
    history_.push_back(entry);
    if (history_.size() > kHistoryDepth) history_.pop_front();
}

// ---------------------------------------------------------------------------
// One cycle
// ---------------------------------------------------------------------------

StopEvent Pipeline::tick() {
    // Shift, back half first so nothing is overwritten before it has moved.
    slots_[index_of(Stage::WB)] = slots_[index_of(Stage::MEM)];
    slots_[index_of(Stage::MEM)] = slots_[index_of(Stage::EX)];
    slots_[index_of(Stage::EX)] = slots_[index_of(Stage::ID)];
    slots_[index_of(Stage::ID)] = slots_[index_of(Stage::IF)];
    slots_[index_of(Stage::IF)] = Slot{};

    StopEvent event;
    u8 flushed = 0;
    bool committed = false;

    // A halted machine still drains: what is already in flight keeps moving
    // along, it simply has nothing following it.
    Slot& ex = slots_[index_of(Stage::EX)];
    const bool runnable = ex.valid && session_->has_program() && !session_->halted();

    if (runnable && ex.pc != session_->cpu().pc) {
        // The machine moved without going through here. Rather than run an
        // instruction the pc has left behind, adopt where it actually is.
        resync();
        return make_step_event();
    }

    if (runnable) {
        // The front-end only ever runs ahead sequentially, and every redirect
        // discards what was fetched behind it, so the instruction arriving here
        // is the one the machine is about to run. This is where the whole
        // instruction happens: fetch, decode, execute and commit, all of it.
        const Addr sequential = ex.pc + ex.instr.length;
        event = session_->step(StepKind::Instruction);
        committed = true;
        ex.committed = true;

        if (session_->cpu().pc != sequential) {
            // A taken branch, a jump, a trap or an interrupt -- they all look
            // the same from here, which is why none of them needs its own case.
            // Whatever was fetched behind this was on the path not taken.
            for (const Stage stage : {Stage::ID, Stage::IF}) {
                if (slots_[index_of(stage)].valid) ++flushed;
                slots_[index_of(stage)] = Slot{};
            }
            fetch_pc_ = session_->cpu().pc;
        }
    }

    // Fetch, unless the machine has stopped for good. A breakpoint still
    // fetches: the instruction it guards belongs in IF, fetched and not run,
    // which is exactly where the machine is standing.
    if (session_->has_program() && !session_->halted()) {
        Slot& fetched = slots_[index_of(Stage::IF)];
        fetched = fetch(fetch_pc_);
        fetch_pc_ += fetched.instr.length;
    }

    ++ticks_;
    last_flushed_ = flushed;
    record(committed, flushed);
    return committed ? event : make_step_event();
}

bool Pipeline::reverse_tick() {
    // The first record is where the pipe started, not a cycle that ran.
    if (history_.size() < 2) return false;
    if (history_.back().committed && !session_->can_reverse()) return false;

    const bool undo_instruction = history_.back().committed;
    history_.pop_back();
    if (undo_instruction) session_->reverse_step(StepKind::Instruction);

    const CycleRecord& now = history_.back();
    slots_ = now.stages;
    fetch_pc_ = now.fetch_pc;
    ticks_ = now.tick;
    last_flushed_ = now.flushed;
    return true;
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

std::string_view Pipeline::mnemonic(Stage stage) const {
    const Slot& slot = at(stage);
    if (!slot.valid) return {};
    if (!slot.instr.valid()) return "??";
    return slot.instr.desc().mnemonic;
}

bool Pipeline::drained() const {
    for (const Slot& slot : slots_) {
        if (slot.valid) return false;
    }
    return true;
}

StopEvent Pipeline::make_step_event() const {
    StopEvent event;
    event.reason = StopReason::Step;
    event.pc = session_->cpu().pc;
    event.line = session_->current_line();
    return event;
}

}  // namespace rv::dbg
