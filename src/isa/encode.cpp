#include "isa/encode.hpp"

#include "isa/csr.hpp"
#include "isa/fields.hpp"

namespace rv::isa {
namespace {

EncodeResult range_error(EncodeError error, const char* field, i64 value, i64 min, i64 max) {
    EncodeResult result;
    result.error = error;
    result.field = field;
    result.value = value;
    result.min = min;
    result.max = max;
    return result;
}

}  // namespace

EncodeResult encode(InstrId id, const Operands& operands) {
    const InstrDesc& desc = describe(id);

    if (operands.rd >= kNumRegs || operands.rs1 >= kNumRegs || operands.rs2 >= kNumRegs) {
        return range_error(EncodeError::RegisterOutOfRange, "register", operands.rd, 0,
                           kNumRegs - 1);
    }

    EncodeResult result;
    result.word = desc.match;
    const i64 imm = operands.imm;

    switch (desc.syntax) {
        case OperandSyntax::NONE:
            break;

        case OperandSyntax::RD_RS1_RS2:
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) | put_rs2(operands.rs2);
            break;

        case OperandSyntax::RD_RS1_IMM:
        case OperandSyntax::RD_OFS_RS1:
            if (!fits_signed(imm, 12)) {
                return range_error(EncodeError::ImmediateOutOfRange, "immediate", imm, -2048, 2047);
            }
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) | put_imm_i(operands.imm);
            break;

        case OperandSyntax::RD_RS1_SHAMT:
            if (imm < 0 || imm > 31) {
                return range_error(EncodeError::ShiftAmountOutOfRange, "shift amount", imm, 0, 31);
            }
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) |
                           put_shamt(static_cast<u32>(imm));
            break;

        case OperandSyntax::RS2_OFS_RS1:
            if (!fits_signed(imm, 12)) {
                return range_error(EncodeError::ImmediateOutOfRange, "offset", imm, -2048, 2047);
            }
            result.word |= put_rs1(operands.rs1) | put_rs2(operands.rs2) | put_imm_s(operands.imm);
            break;

        case OperandSyntax::RS1_RS2_LBL:
            if ((imm & 1) != 0) {
                return range_error(EncodeError::ImmediateMisaligned, "branch offset", imm, 0, 0);
            }
            if (!fits_signed(imm, 13)) {
                return range_error(EncodeError::ImmediateOutOfRange, "branch offset", imm, -4096,
                                   4094);
            }
            result.word |= put_rs1(operands.rs1) | put_rs2(operands.rs2) | put_imm_b(operands.imm);
            break;

        case OperandSyntax::RD_UIMM:
            if ((static_cast<u32>(operands.imm) & 0xfffu) != 0) {
                return range_error(EncodeError::ImmediateNotShifted, "upper immediate", imm, 0, 0);
            }
            result.word |= put_rd(operands.rd) | put_imm_u(operands.imm);
            break;

        case OperandSyntax::RD_LBL:
            if ((imm & 1) != 0) {
                return range_error(EncodeError::ImmediateMisaligned, "jump offset", imm, 0, 0);
            }
            if (!fits_signed(imm, 21)) {
                return range_error(EncodeError::ImmediateOutOfRange, "jump offset", imm, -1048576,
                                   1048574);
            }
            result.word |= put_rd(operands.rd) | put_imm_j(operands.imm);
            break;

        case OperandSyntax::RD_CSR_RS1:
            if (operands.csr > 0xfff) {
                return range_error(EncodeError::CsrOutOfRange, "csr", operands.csr, 0, 0xfff);
            }
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) | put_csr(operands.csr);
            break;

        case OperandSyntax::RD_CSR_ZIMM:
            if (operands.csr > 0xfff) {
                return range_error(EncodeError::CsrOutOfRange, "csr", operands.csr, 0, 0xfff);
            }
            if (imm < 0 || imm > 31) {
                return range_error(EncodeError::ImmediateOutOfRange, "immediate", imm, 0, 31);
            }
            result.word |= put_rd(operands.rd) | put_zimm(static_cast<u32>(imm)) |
                           put_csr(operands.csr);
            break;
    }

    return result;
}

Operands operands_of(const DecodedInstr& instr) {
    Operands operands;
    operands.rd = instr.rd;
    operands.rs1 = instr.rs1;
    operands.rs2 = instr.rs2;
    operands.imm = instr.imm;
    operands.csr = instr.csr;
    return operands;
}

const char* encode_error_message(EncodeError error) {
    switch (error) {
        case EncodeError::None: return "ok";
        case EncodeError::ImmediateOutOfRange: return "immediate out of range";
        case EncodeError::ImmediateMisaligned: return "target must be 2-byte aligned";
        case EncodeError::ImmediateNotShifted: return "upper immediate must have zero low 12 bits";
        case EncodeError::ShiftAmountOutOfRange: return "shift amount out of range";
        case EncodeError::CsrOutOfRange: return "csr address out of range";
        case EncodeError::RegisterOutOfRange: return "register number out of range";
    }
    return "unknown encoding error";
}

}  // namespace rv::isa
