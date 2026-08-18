// rv32 -- a RISC-V RV32IM_Zicsr emulator, assembler and debugger.
//
// The command handling here is deliberately thin. From M4 the real command set
// lives in rv_cmd, shared by the CLI and the TUI, and this file shrinks to
// argument parsing and dispatch.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/hart.hpp"
#include "core/memfile.hpp"
#include "isa/disasm.hpp"
#include "isa/regnames.hpp"

namespace {

using namespace rv;

constexpr const char* kVersion = "rv32 0.1.0 (RV32IM_Zicsr)";

void print_usage() {
    std::puts(
        "usage: rv32 <command> [options]\n"
        "\n"
        "commands:\n"
        "  exec <word>...        execute raw instruction words given as hex\n"
        "  run <imem.mem>        load a $readmemh image and run it\n"
        "  dis <imem.mem>        disassemble a $readmemh image\n"
        "  --version, --help\n"
        "\n"
        "options:\n"
        "  --dmem <file>         also load a data memory image (run)\n"
        "  --trace               print each instruction as it executes (run)\n"
        "  --limit <n>           stop after n instructions (default 1000000)\n"
        "  --numeric             print x10 rather than a0\n"
        "  --quiet               only print the final state\n");
}

struct Options {
    std::string dmem_path;
    u64 limit = 1'000'000;
    bool trace = false;
    bool quiet = false;
    bool numeric = false;
    std::vector<std::string> positional;
};

/// Returns false and prints a message if an option is malformed.
bool parse_options(const std::vector<std::string>& args, Options& out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--trace") {
            out.trace = true;
        } else if (arg == "--quiet") {
            out.quiet = true;
        } else if (arg == "--numeric") {
            out.numeric = true;
        } else if (arg == "--limit" || arg == "--dmem") {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "rv32: %s needs a value\n", arg.c_str());
                return false;
            }
            if (arg == "--limit") {
                out.limit = std::strtoull(args[++i].c_str(), nullptr, 0);
            } else {
                out.dmem_path = args[++i];
            }
        } else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "rv32: unknown option '%s'\n", arg.c_str());
            return false;
        } else {
            out.positional.push_back(arg);
        }
    }
    return true;
}

/// Show only the registers that are not zero. Printing all 32 buries the two or
/// three a small program actually touched.
void print_nonzero_registers(const core::CpuState& cpu, bool numeric) {
    std::printf("registers (non-zero only):\n");
    bool any = false;
    for (RegIdx r = 1; r < kNumRegs; ++r) {
        if (cpu.x[r] == 0) continue;
        any = true;
        if (numeric) {
            std::printf("  %-4s 0x%08x  %d\n", isa::numeric_name(r).data(), cpu.x[r],
                        static_cast<i32>(cpu.x[r]));
        } else {
            std::printf("  %-3s %-4s 0x%08x  %d\n", isa::numeric_name(r).data(),
                        isa::abi_name(r).data(), cpu.x[r], static_cast<i32>(cpu.x[r]));
        }
    }
    if (!any) std::printf("  (all zero)\n");
    std::printf("  pc       0x%08x   cycle %llu  instret %llu\n", cpu.pc,
                static_cast<unsigned long long>(cpu.cycle),
                static_cast<unsigned long long>(cpu.instret));
}

void print_trace_line(const core::StepOutcome& outcome, const core::Hart& hart, bool numeric) {
    isa::DisasmOptions disasm_options;
    disasm_options.abi_names = !numeric;
    const std::string text = isa::disassemble(outcome.instr, outcome.pc_before, disasm_options);

    std::printf("%08x  %08x  %-26s", outcome.pc_before, outcome.raw, text.c_str());
    if (outcome.reg_written != core::kNoReg) {
        const RegIdx reg = outcome.reg_written;
        std::printf("%s = 0x%08x",
                    numeric ? isa::numeric_name(reg).data() : isa::abi_name(reg).data(),
                    hart.cpu().x[reg]);
    } else if (outcome.mem_written != core::kNoAddr) {
        std::printf("[0x%08x] <- %u bytes", outcome.mem_written, outcome.mem_width);
    }
    if (outcome.trapped) std::printf("  TRAP: %s", core::trap_cause_name(outcome.cause));
    std::printf("\n");
}

/// Run to completion and report why it stopped.
void run_machine(core::Hart& hart, const Options& options) {
    if (options.trace) {
        std::printf("addr      word      instruction               effect\n");
        std::printf("---------------------------------------------------------------\n");
    }

    u64 executed = 0;
    while (!hart.halted() && executed < options.limit) {
        const core::StepOutcome outcome = hart.step();
        ++executed;
        if (options.trace) print_trace_line(outcome, hart, options.numeric);
        if (outcome.halt != core::HaltReason::None) break;
    }

    if (hart.halted()) {
        std::printf("\nstopped: %s\n", core::halt_reason_name(hart.halt_reason()));
    } else {
        // Almost always an infinite loop. Say so rather than just stopping.
        std::printf("\nstopped: instruction limit (%llu) reached -- infinite loop?\n",
                    static_cast<unsigned long long>(options.limit));
    }

    if (!options.quiet) {
        std::printf("\n");
        print_nonzero_registers(hart.cpu(), options.numeric);
    }
    const std::string& uart = hart.bus().uart().output();
    if (!uart.empty()) std::printf("\nuart output:\n%s\n", uart.c_str());
}

int command_exec(const std::vector<std::string>& args) {
    Options options;
    options.limit = 1000;
    options.trace = true;
    if (!parse_options(args, options)) return 2;

    std::vector<Word> words;
    for (const std::string& text : options.positional) {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 16);
        if (end == text.c_str() || *end != '\0') {
            std::fprintf(stderr, "rv32: '%s' is not a hex instruction word\n", text.c_str());
            return 2;
        }
        words.push_back(static_cast<Word>(value));
    }
    if (words.empty()) {
        std::fprintf(stderr, "rv32: no instruction words given\n");
        return 2;
    }

    core::Hart hart;
    hart.imem().load_words(words);
    hart.reset(0);
    run_machine(hart, options);
    return 0;
}

int command_run(const std::vector<std::string>& args) {
    Options options;
    if (!parse_options(args, options)) return 2;
    if (options.positional.size() != 1) {
        std::fprintf(stderr, "rv32: run needs exactly one image file\n");
        return 2;
    }

    std::vector<Word> imem_words;
    if (const core::MemFileResult result = core::read_mem_file(options.positional[0], imem_words);
        !result.ok) {
        std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
        return 1;
    }

    core::Hart hart;
    hart.imem().load_words(imem_words);

    if (!options.dmem_path.empty()) {
        std::vector<Word> dmem_words;
        if (const core::MemFileResult result = core::read_mem_file(options.dmem_path, dmem_words);
            !result.ok) {
            std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
            return 1;
        }
        hart.reset(0);
        hart.bus().dmem().load_words(dmem_words);
    } else {
        hart.reset(0);
    }

    run_machine(hart, options);
    return 0;
}

int command_dis(const std::vector<std::string>& args) {
    Options options;
    if (!parse_options(args, options)) return 2;
    if (options.positional.size() != 1) {
        std::fprintf(stderr, "rv32: dis needs exactly one image file\n");
        return 2;
    }

    std::vector<Word> words;
    if (const core::MemFileResult result = core::read_mem_file(options.positional[0], words);
        !result.ok) {
        std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
        return 1;
    }

    isa::DisasmOptions disasm_options;
    disasm_options.abi_names = !options.numeric;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const Addr address = static_cast<Addr>(i * 4);
        std::printf("%08x:  %08x  %s\n", address, words[i],
                    isa::disassemble_word(words[i], address, disasm_options).c_str());
    }
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

    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (args[0] == "exec") return command_exec(rest);
    if (args[0] == "run") return command_run(rest);
    if (args[0] == "dis") return command_dis(rest);

    std::fprintf(stderr, "rv32: unknown command '%s'\n", args[0].c_str());
    print_usage();
    return 2;
}
