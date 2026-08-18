// rv32 -- a RISC-V RV32IM_Zicsr emulator, assembler and debugger.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/hart.hpp"
#include "isa/disasm.hpp"
#include "isa/regnames.hpp"

namespace {

constexpr const char* kVersion = "rv32 0.1.0 (RV32IM_Zicsr)";

void print_usage() {
    std::puts(
        "usage: rv32 <command> [options]\n"
        "\n"
        "commands:\n"
        "  exec <word>...    execute raw instruction words given as hex\n"
        "  --version         print the version and exit\n"
        "  --help            print this message\n"
        "\n"
        "options for exec:\n"
        "  --limit <n>       stop after n instructions (default 1000)\n"
        "  --quiet           only print the final register state\n");
}

/// Show the registers that are not zero. Printing all 32 buries the two or
/// three that a small program actually touched.
void print_nonzero_registers(const rv::core::CpuState& cpu) {
    std::printf("registers (non-zero only):\n");
    bool any = false;
    for (rv::RegIdx r = 1; r < rv::kNumRegs; ++r) {
        if (cpu.x[r] == 0) continue;
        any = true;
        std::printf("  %-3s %-4s 0x%08x  %d\n", rv::isa::numeric_name(r).data(),
                    rv::isa::abi_name(r).data(), cpu.x[r], static_cast<rv::i32>(cpu.x[r]));
    }
    if (!any) std::printf("  (all zero)\n");
    std::printf("  pc       0x%08x   cycle %llu  instret %llu\n", cpu.pc,
                static_cast<unsigned long long>(cpu.cycle),
                static_cast<unsigned long long>(cpu.instret));
}

int command_exec(const std::vector<std::string>& args) {
    std::vector<rv::u32> words;
    rv::u64 limit = 1000;
    bool quiet = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--limit") {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "rv32: --limit needs a value\n");
                return 2;
            }
            limit = std::strtoull(args[++i].c_str(), nullptr, 0);
        } else {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(arg.c_str(), &end, 16);
            if (end == arg.c_str() || *end != '\0') {
                std::fprintf(stderr, "rv32: '%s' is not a hex instruction word\n", arg.c_str());
                return 2;
            }
            words.push_back(static_cast<rv::u32>(value));
        }
    }

    if (words.empty()) {
        std::fprintf(stderr, "rv32: no instruction words given\n");
        return 2;
    }

    rv::core::Hart hart;
    hart.imem().load_words(words);
    hart.reset(0);

    if (!quiet) {
        std::printf("addr      word      instruction              effect\n");
        std::printf("-------------------------------------------------------------\n");
    }

    rv::u64 executed = 0;
    while (!hart.halted() && executed < limit) {
        const rv::core::StepOutcome outcome = hart.step();
        ++executed;

        if (!quiet) {
            const std::string text = rv::isa::disassemble(outcome.instr, outcome.pc_before);
            std::printf("%08x  %08x  %-24s", outcome.pc_before, outcome.raw, text.c_str());
            if (outcome.reg_written != rv::core::kNoReg) {
                std::printf("%s = 0x%08x", rv::isa::abi_name(outcome.reg_written).data(),
                            hart.cpu().x[outcome.reg_written]);
            } else if (outcome.mem_written != rv::core::kNoAddr) {
                std::printf("[0x%08x] written", outcome.mem_written);
            }
            if (outcome.trapped) {
                std::printf("  TRAP: %s", rv::core::trap_cause_name(outcome.cause));
            }
            std::printf("\n");
        }

        if (outcome.halt != rv::core::HaltReason::None) break;
    }

    if (executed >= limit && !hart.halted()) {
        std::printf("\nstopped: instruction limit (%llu) reached\n",
                    static_cast<unsigned long long>(limit));
    } else if (hart.halted()) {
        std::printf("\nstopped: %s\n", rv::core::halt_reason_name(hart.halt_reason()));
    }

    std::printf("\n");
    print_nonzero_registers(hart.cpu());

    const std::string& output = hart.bus().uart().output();
    if (!output.empty()) std::printf("\nuart output:\n%s\n", output.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version") {
        std::puts(kVersion);
        return 0;
    }
    if (args[0] == "exec") {
        return command_exec({args.begin() + 1, args.end()});
    }

    std::fprintf(stderr, "rv32: unknown command '%s'\n", args[0].c_str());
    print_usage();
    return 2;
}
