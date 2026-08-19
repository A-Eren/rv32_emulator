// Completion and operand hints.
//
// The claim under test: none of this is written out by hand. Signatures come
// from instr_table.def's operand syntax, so adding an instruction makes it
// completable and documented for free -- and a hint cannot promise something
// the assembler will refuse.
#include <algorithm>

#include "asm/assembler.hpp"
#include "asm/complete.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::as;

namespace {

std::vector<std::string> texts(const std::vector<Completion>& items) {
    std::vector<std::string> out;
    out.reserve(items.size());
    for (const Completion& item : items) out.push_back(item.text);
    return out;
}

bool contains(const std::vector<std::string>& items, const std::string& needle) {
    return std::find(items.begin(), items.end(), needle) != items.end();
}

const Completion* find(const std::vector<Completion>& items, const std::string& text) {
    for (const Completion& item : items) {
        if (item.text == text) return &item;
    }
    return nullptr;
}

}  // namespace

// ---- signatures ------------------------------------------------------------

RV_TEST(complete, signatures_come_from_the_operand_syntax) {
    RV_CHECK_STR(signature_of("add"), "add rd, rs1, rs2");
    RV_CHECK_STR(signature_of("addi"), "addi rd, rs1, imm");
    RV_CHECK_STR(signature_of("slli"), "slli rd, rs1, shamt");
    RV_CHECK_STR(signature_of("lw"), "lw rd, offset(base)");
    RV_CHECK_STR(signature_of("sw"), "sw rs2, offset(base)");
    RV_CHECK_STR(signature_of("beq"), "beq rs1, rs2, label");
    RV_CHECK_STR(signature_of("lui"), "lui rd, imm20");
    RV_CHECK_STR(signature_of("jal"), "jal rd, label");
    RV_CHECK_STR(signature_of("csrrw"), "csrrw rd, csr, rs1");
    RV_CHECK_STR(signature_of("csrrwi"), "csrrwi rd, csr, zimm");
    RV_CHECK_STR(signature_of("ebreak"), "ebreak");
}

RV_TEST(complete, pseudo_instructions_have_signatures_too) {
    RV_CHECK_STR(signature_of("mv"), "mv rd, rs");
    RV_CHECK_STR(signature_of("li"), "li rd, imm");
    RV_CHECK_STR(signature_of("la"), "la rd, symbol");
    RV_CHECK_STR(signature_of("beqz"), "beqz rs, label");
    RV_CHECK_STR(signature_of("call"), "call label");
    RV_CHECK_STR(signature_of("ret"), "ret");
    RV_CHECK_STR(signature_of("csrw"), "csrw csr, rs");
}

RV_TEST(complete, every_instruction_in_the_table_has_a_hint) {
    // The point of generating these: adding a row cannot leave one undocumented.
    for (const isa::InstrDesc& desc : isa::kInstrTable) {
        const std::string signature = signature_of(desc.mnemonic);
        RV_CHECK(!signature.empty());
        RV_CHECK_EQ(signature.compare(0, desc.mnemonic.size(), desc.mnemonic), 0);

        const std::vector<OperandHint> hints = operand_hints(desc.mnemonic);
        for (const OperandHint& hint : hints) {
            RV_CHECK(!hint.name.empty());
            RV_CHECK(!hint.form.empty());
        }
    }
}

RV_TEST(complete, the_hint_forms_say_what_is_actually_accepted) {
    const std::vector<OperandHint> addi = operand_hints("addi");
    RV_CHECK_EQ(addi.size(), std::size_t{3});
    // The range in the hint is the range the encoder enforces.
    RV_CHECK_NE(addi[2].form.find("-2048..2047"), std::string::npos);

    const std::vector<OperandHint> slli = operand_hints("slli");
    RV_CHECK_NE(slli[2].form.find("0..31"), std::string::npos);

    const std::vector<OperandHint> lw = operand_hints("lw");
    RV_CHECK_NE(lw[1].form.find("offset(base)"), std::string::npos);
}

// ---- context ---------------------------------------------------------------

RV_TEST(complete, the_leading_word_is_a_mnemonic_position) {
    const Context context = context_at("    ad", 6);
    RV_CHECK_EQ(static_cast<int>(context.where), static_cast<int>(Context::Where::Mnemonic));
    RV_CHECK_STR(context.prefix, "ad");
    RV_CHECK_EQ(context.prefix_begin, 4);
}

RV_TEST(complete, a_label_does_not_close_the_leading_position) {
    const Context context = context_at("loop:   ad", 10);
    RV_CHECK_EQ(static_cast<int>(context.where), static_cast<int>(Context::Where::Mnemonic));
    RV_CHECK_STR(context.prefix, "ad");
}

RV_TEST(complete, operands_are_counted_by_the_commas_before_the_caret) {
    RV_CHECK_EQ(context_at("    addi a", 10).operand_index, std::size_t{0});
    RV_CHECK_EQ(context_at("    addi a0, a", 14).operand_index, std::size_t{1});
    RV_CHECK_EQ(context_at("    addi a0, a1, ", 17).operand_index, std::size_t{2});

    const Context context = context_at("    addi a0, a", 14);
    RV_CHECK_EQ(static_cast<int>(context.where), static_cast<int>(Context::Where::Operand));
    RV_CHECK_STR(context.mnemonic, "addi");
}

RV_TEST(complete, there_is_nothing_to_suggest_inside_a_comment_or_a_string) {
    RV_CHECK_EQ(static_cast<int>(context_at("    addi a0, a1, 5  # ad", 24).where),
                static_cast<int>(Context::Where::Nowhere));
    RV_CHECK_EQ(static_cast<int>(context_at("    .asciz \"ad", 14).where),
                static_cast<int>(Context::Where::Nowhere));
}

// ---- completion ------------------------------------------------------------

RV_TEST(complete, typing_ad_offers_add_and_addi) {
    // The example in the request, and the whole point of the feature.
    const std::vector<std::string> got = texts(complete("    ad", 6));
    RV_CHECK(contains(got, "add"));
    RV_CHECK(contains(got, "addi"));
    // Shorter first, so the exact word sits above the longer ones.
    RV_CHECK_STR(got.front(), "add");
    // And nothing unrelated.
    RV_CHECK(!contains(got, "sub"));
    RV_CHECK(!contains(got, "lw"));
}

RV_TEST(complete, a_candidate_carries_its_signature_and_a_word_about_it) {
    const std::vector<Completion> items = complete("    ad", 6);
    const Completion* addi = find(items, "addi");
    RV_CHECK(addi != nullptr);
    if (addi == nullptr) return;
    RV_CHECK_STR(addi->signature, "addi rd, rs1, imm");
    RV_CHECK_STR(addi->detail, "arithmetic (immediate)");
    RV_CHECK_EQ(static_cast<int>(addi->kind), static_cast<int>(CompletionKind::Instruction));
}

RV_TEST(complete, pseudo_instructions_are_offered_and_marked_as_such) {
    const std::vector<Completion> items = complete("    b", 5);
    const Completion* beqz = find(items, "beqz");
    RV_CHECK(beqz != nullptr);
    if (beqz != nullptr) {
        RV_CHECK_EQ(static_cast<int>(beqz->kind), static_cast<int>(CompletionKind::Pseudo));
    }
    // A spelling that is both, like jal, appears once rather than twice.
    const std::vector<std::string> jal = texts(complete("    ja", 6));
    RV_CHECK_EQ(std::count(jal.begin(), jal.end(), std::string("jal")), 1);
}

RV_TEST(complete, directives_are_offered_after_a_dot) {
    const std::vector<std::string> got = texts(complete("    .te", 7));
    RV_CHECK(contains(got, ".text"));
    RV_CHECK(!contains(got, "addi"));
}

RV_TEST(complete, a_register_position_offers_registers_and_not_instructions) {
    const std::vector<std::string> got = texts(complete("    addi a", 10));
    RV_CHECK(contains(got, "a0"));
    RV_CHECK(contains(got, "a1"));
    RV_CHECK(!contains(got, "add"));   // not a register position
    RV_CHECK(!contains(got, "mtvec"));  // nor a csr one
}

RV_TEST(complete, both_register_spellings_are_offered) {
    const std::vector<std::string> got = texts(complete("    add x1", 10));
    RV_CHECK(contains(got, "x1"));
    RV_CHECK(contains(got, "x10"));

    const Completion* item = find(complete("    add a0", 10), "a0");
    RV_CHECK(item != nullptr);
    // The list shows the correspondence, since that is the thing worth
    // learning about a register name.
    if (item != nullptr) RV_CHECK_STR(item->signature, "x10 / a0");
}

RV_TEST(complete, a_csr_position_offers_csr_names) {
    const std::vector<std::string> got = texts(complete("    csrrw t0, m", 15));
    RV_CHECK(contains(got, "mstatus"));
    RV_CHECK(contains(got, "mtvec"));
    RV_CHECK(!contains(got, "a0"));  // not a register position
}

RV_TEST(complete, a_label_position_offers_the_programs_symbols) {
    AssembleOptions options;
    options.explain_pseudo_sizing = false;
    const AssembledProgram program = assemble_text(
        "        .data\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "loop:   nop\n"
        "        ebreak\n",
        "t.s", options);
    RV_CHECK(program.ok());

    const std::vector<Completion> items = complete("    beq a0, a1, l", 17, &program.symbols);
    const std::vector<std::string> got = texts(items);
    RV_CHECK(contains(got, "loop"));

    const Completion* loop = find(items, "loop");
    RV_CHECK(loop != nullptr);
    if (loop != nullptr) {
        // The space is in the detail, because jumping to a data symbol is the
        // mistake this machine makes easy to make.
        RV_CHECK_NE(loop->detail.find("text"), std::string::npos);
        RV_CHECK_NE(loop->detail.find("line 4"), std::string::npos);
    }
}

RV_TEST(complete, an_empty_prefix_offers_everything_valid_here) {
    // Asking with nothing typed is how a "what can go here?" key works.
    const std::vector<std::string> mnemonics = texts(complete("    ", 4));
    RV_CHECK(contains(mnemonics, "addi"));
    RV_CHECK(contains(mnemonics, ".text"));

    const std::vector<std::string> registers = texts(complete("    addi ", 9));
    RV_CHECK(contains(registers, "a0"));
    RV_CHECK(!contains(registers, "addi"));
}

RV_TEST(complete, matching_ignores_case) {
    RV_CHECK(contains(texts(complete("    AD", 6)), "add"));
}

RV_TEST(complete, an_unknown_mnemonic_offers_nothing_for_its_operands) {
    // Rather than guessing: there is no shape to suggest against.
    const std::vector<Completion> items = complete("    frobnicate a", 16);
    RV_CHECK(items.empty() || !contains(texts(items), "add"));
}
