// The smaller dock panels.
#pragma once

#include "dbg/session.hpp"
#include "tui/widget.hpp"

namespace rv::tui {

/// A hex dump of data memory, which follows the word the last store touched.
/// Auto-following matters more than it sounds: a store is invisible unless the
/// view happens to be looking at the right address.
class MemView final : public Widget {
public:
    explicit MemView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    bool on_key(const Key& key) override;
    std::string title() const override { return "Mem"; }

    void set_address(Addr addr) { base_ = addr & ~0xfu; }
    Addr address() const { return base_; }
    /// Jump to whatever the last step wrote, if it wrote anything.
    void follow_last_store();

private:
    const dbg::DebugSession* session_;
    Addr base_ = 0;
    Addr highlight_ = core::kNoAddr;
};

/// Whatever the program has sent to the UART.
class ConsoleView final : public Widget {
public:
    explicit ConsoleView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    std::string title() const override { return "Out"; }

private:
    const dbg::DebugSession* session_;
};

/// The memory-mapped devices, as state rather than as addresses.
class MmioView final : public Widget {
public:
    explicit MmioView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    std::string title() const override { return "I/O"; }

private:
    const dbg::DebugSession* session_;
};

/// Every symbol, with the address space it lives in -- which is the thing that
/// explains why `call msg` was rejected.
class SymbolsView final : public Widget {
public:
    explicit SymbolsView(const dbg::DebugSession& session) : session_(&session) {}

    void draw(Pane pane) override;
    bool on_key(const Key& key) override;
    std::string title() const override { return "Sym"; }

private:
    const dbg::DebugSession* session_;
    int top_ = 0;
};

}  // namespace rv::tui
