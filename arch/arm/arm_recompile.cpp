#include "libcpu.h"
#include "cpu_generic.h"
#include "arm_internal.h"
#include "tag.h"

#include <typeinfo>
#include <iostream>

using namespace llvm;

extern Value* ptr_PC;

static Value* ptr_N;
static Value* ptr_Z;
static Value* ptr_C;
static Value* ptr_V;
static Value* ptr_I;

#define ptr_CPSR cpu->ptr_r32[16]

#define BAD do { printf("%s:%d\n", __func__, __LINE__); exit(1); } while(0)
#define LOG do { printf("%s:%d\n", __func__, __LINE__); } while(0)

#define ARM_BRANCH_TARGET ((((int)BITS(0,23) << 8) >> 6) + pc + 8)

#define RD ((instr >> 12) & 0xF)
#define RN ((instr >> 16) & 0xF)
#define RM (instr & 0xF)
#define I ((instr >> 25) & 1)
#define BIT(n) ((instr >> (n)) & 1)
#define BITS(a,b) ((instr >> (a)) & ((1 << (1+(b)-(a)))-1))
#define S BIT(20)

#define GETADDR(r) ((r==15)?(armregs[15]&r15mask):armregs[r])

//////////////////////////////////////////////////////////////////////
// tagging
//////////////////////////////////////////////////////////////////////

int arch_arm_tag_instr(cpu_t *cpu, addr_t pc, tag_t *tag, addr_t *new_pc, addr_t *next_pc) {
	uint32_t instr = *(uint32_t*)&cpu->RAM[pc];

	if (instr == 0xE1A0F00E) {/* MOV r15, r0, r14 */
		*tag = TAG_RET;
		return 0;
	} else if (BITS(24,27) == 10) { /* branch */
		*new_pc = ARM_BRANCH_TARGET;
		*tag = TAG_BRANCH;
	} else if (BITS(24,27) == 11) { /* branch and link */
		*new_pc = ARM_BRANCH_TARGET;
		*tag = TAG_CALL;
	} else if (BITS(20,27) == 18 && BIT(4)) { /* branch exchange */
		*tag = TAG_RET; 
	} else 
		*tag = TAG_CONTINUE;

	if (instr >> 28 != 0xE)
		*tag |= TAG_CONDITIONAL;

	*next_pc = pc + 4;
	return 4;
}

static inline unsigned shift4(unsigned opcode)
{
	BAD;
#if 0
        unsigned shiftmode=opcode&0x60;
        unsigned shiftamount=(opcode&0x10)?(armregs[(opcode>>8)&15]&0xFF):((opcode>>7)&31);
        uint32_t rm=armregs[RM];
        if ((shiftamount-1)>=31)
        {
                return shift5(opcode,shiftmode,shiftamount,rm);
        }
        else
        {
                switch (shiftmode)
                {
                        case 0: /*LSL*/
                        return rm<<shiftamount;
                        case 0x20: /*LSR*/
                        return rm>>shiftamount;
                        case 0x40: /*ASR*/
                        return (int)rm>>shiftamount;
                        default: /*ROR*/
                        return (rm>>shiftamount)|(rm<<(32-shiftamount));
                }
        }
#endif
}

#define shift2(o) ((o&0xFF0)?shift4(o):armregs[RM])

Value *
arch_arm_recompile_cond(cpu_t *cpu, addr_t pc, BasicBlock *bb) {
	switch (*(uint32_t*)&cpu->RAM[pc] >> 28) {
		case 0x0: /* EQ */
			return LOAD(ptr_Z);
		case 0x1: /* NE */
			return NOT1(LOAD(ptr_Z));
		case 0x2: /* CS */
			return LOAD(ptr_C);
		case 0x3: /* CC */
			return NOT1(LOAD(ptr_C));
		case 0x4: /* MI */
			return LOAD(ptr_N);
		case 0x5: /* PL */
			return NOT1(LOAD(ptr_N));
		case 0x6: /* VS */
			return LOAD(ptr_V);
		case 0x7: /* VC */
			return NOT1(LOAD(ptr_V));
		case 0x8: /* HI */
			return AND(LOAD(ptr_C),NOT1(LOAD(ptr_Z)));
		case 0x9: /* LS */
			return NOT1(AND(LOAD(ptr_C),NOT1(LOAD(ptr_Z))));
		case 0xA: /* GE */
			return ICMP_EQ(LOAD(ptr_N),LOAD(ptr_V));
		case 0xB: /* LT */
			return NOT1(ICMP_EQ(LOAD(ptr_N),LOAD(ptr_V)));
		case 0xC: /* GT */
			return AND(NOT1(LOAD(ptr_Z)),ICMP_EQ(LOAD(ptr_N),LOAD(ptr_V)));
		case 0xD: /* LE */
			return NOT1(AND(NOT1(LOAD(ptr_Z)),ICMP_EQ(LOAD(ptr_N),LOAD(ptr_V))));
		case 0xE: /* AL */
			return NULL; /* no condition; this should never happen */
		case 0xF: /* NV */
			return FALSE;
	}
}

Value *operand(cpu_t *cpu, addr_t pc, BasicBlock *bb)
{
	uint32_t instr = *(uint32_t*)&cpu->RAM[pc];
	if (I) { /* 32-bit immediate */
		//XXX TODO: shifter carry out
		uint32_t immed_8 = instr & 0xFF;
		int rotate_imm = ((instr >> 8) & 0xF) << 1;
		return CONST((immed_8 >> rotate_imm) | (immed_8 << (32 - rotate_imm)));
	} else {
		if (!BIT(4)) { /* Immediate shifts */
			int shift = BITS(5,6);
			int shift_imm = BITS(7,11);
			printf("shift=%x\n", shift);
			printf("shift_imm=%x\n", shift_imm);
			if (!shift && !shift_imm) { /* Register */
				return R(RM);
			} else {
				BAD;
			}
		} else {
			if (!BIT(7)) { /* Register shifts */
				BAD;
			} else { /* arithmetic or Load/Store instruction extension space */
				BAD;
			}
		}
	}
}
#define OPERAND operand(cpu,pc,bb)


static void
setsub(cpu_t *cpu, Value *op1, Value *op2, BasicBlock *bb)
{
	Value *v = BinaryOperator::Create(Instruction::Sub, op1, op2, "", bb);
	/* Z */	new StoreInst(ICMP_EQ(v, CONST(0)), ptr_Z, bb);
	/* N */	new StoreInst(ICMP_SLT(v, CONST(0)), ptr_N, bb);
	/* C */	new StoreInst(ICMP_SLE(v, op1), ptr_C, false, bb);
	/* V */	new StoreInst(TRUNC1(LSHR(AND(XOR(op1, op2),XOR(op1,v)),CONST(31))), ptr_V, false, bb);
	return;
}

#define LET1(a,b) new StoreInst(b, a, false, bb)

#define SET_NZ(a) { Value *t = a; LET1(ptr_Z, ICMP_EQ(t, CONST(0))); LET1(ptr_N, ICMP_SLT(t, CONST(0))); }

#define COMPUTE_CARRY(src1, src2, result) \
	(AND(ICMP_NE(src2, CONST(0)), ICMP_ULT(result, src1)))

#define LINK LET32(14, CONST((uint64_t)(sint64_t)(sint32_t)pc+4))

int arch_arm_recompile_instr(cpu_t *cpu, addr_t pc, BasicBlock *bb) {
printf("%s:%d pc=%llx\n", __func__, __LINE__, pc);
	uint32_t instr = *(uint32_t*)&cpu->RAM[pc];

//	int cond = instr >> 28;
//	int op1 = (instr>>20)&0xFF;
//	int op2 = (instr>>4)&0xF;
//	int shift_bits = (instr>>4)&0xFF;
//	printf("cond=%x, op1=%x, op2=%x, shift_bits=%x\n", cond, op1, op2, shift_bits);

	int opcode = ((instr >> 21) & 0x0F);
	printf("opcode=%d\n", opcode);

	switch ((instr >> 26) & 3) { /* bits 26 and 27 */
		case 0:
			switch(opcode) {
				case 0: /* AND */
					{
						Value *op1 = R(RN);
						Value *op2 = OPERAND;
						Value *res = AND(op1,op2);
						LET(RD, res);
					}
					break;
				case 2: /* SUB */
					{
						Value *op1 = R(RN);
						Value *op2 = OPERAND;
						Value *res = SUB(op1,op2);
						LET(RD, res);
						if (S) {
							SET_NZ(res);
							LET1(ptr_C, COMPUTE_CARRY(op1, op2, res));
							BAD;
							// TODO overflow.
						}
					}
					break;
				case 4: /* ADD */
					{
						Value *op1 = R(RN);
						Value *op2 = OPERAND;
						Value *res = ADD(op1,op2);
						LET(RD, res);
						if (S) {
							SET_NZ(res);
							LET1(ptr_C, COMPUTE_CARRY(op1, op2, res));
							BAD;
							//XXX TODO overflow!
						}
					}
					break;
				case 10: /* CMP */
					if (!S) /* CMP without S bit */
						BAD;
					setsub(cpu, R(RN), OPERAND, bb);
					break;
				case 13: /* MOV */
					if (S)
						BAD;
					if (RD == 15) {
						new StoreInst(SUB(R(14),CONST(4)), cpu->ptr_PC, bb);
						break;
					}
					LET(RD, OPERAND);
					break;
				default:
					if (BITS(20,27) == 18 && BIT(4)) {
						new StoreInst(R(BITS(0,3)), cpu->ptr_PC, bb);
						break;
					}
					BAD;
			}
			break;
		case 1: { /* Single Data Transfer (LDR, STR) */
			uint32_t offset = BITS(0, 11);

			if (BIT(20)) { /* Load */
				if (BIT(22)) { /* Load byte */
					BAD;
				} else { /* Load word */
					if (BITS(16, 19) == 15) { /* Handle PC, special case. */
						if (BIT(23)) {
							uint32_t tmp = pc + offset + 8;
							LOAD32(BITS(12, 15), CONST(tmp));
							LET(BITS(12, 15), SWAP32(R(BITS(12, 15))));
						} else
							LOAD32(BITS(12, 15), CONST(pc - offset + 8));
					} else {
						if (BIT(23))
							LOAD32(BITS(12, 15), ADD(R32(BITS(16, 19)), CONST(offset)));
						else
							LOAD32(BITS(12, 15), SUB(R32(BITS(16, 19)), CONST(offset)));
					}
				}
			} else { /* Store */
				if (BIT(22)) { /* Store byte */
					BAD;
				} else { /* Store word */
					if (BITS(16, 19) == 15) { /* Handle PC, special case. */
						BAD;
					} else {
						if (BIT(23))
							STORE32(R(BITS(12, 15)), ADD(R(BITS(16, 19)), CONST(offset)));
						else
							STORE32(R(BITS(12, 15)), SUB(R(BITS(16, 19)), CONST(offset)));
					}
				}
			}

			if (BIT(21) && BIT(23)) {
				LET32(BITS(16, 19), ADD(R(BITS(16, 19)), CONST(offset)));
				printf("W bit set.\n");
			} else if (BIT(21) && !BIT(23)) {
				LET32(BITS(16, 19), SUB(R(BITS(16, 19)), CONST(offset)));
				printf("W bit set.\n");
			}

			break;
		}
		case 2:
			if (BIT(25)) {
				if (BIT(24)) {
					LINK;
				}
				break;
			} else
				BAD;
		case 3:
			BAD;
		
	}

	LOG;
	return 4;
}

#define N_SHIFT 31
#define Z_SHIFT 30
#define C_SHIFT 29
#define V_SHIFT 28
#define I_SHIFT 27

static Value *
arch_arm_flags_encode(BasicBlock *bb)
{
	Value *flags = ConstantInt::get(getIntegerType(32), 0);

	flags = arch_encode_bit(flags, ptr_N, N_SHIFT, 32, bb);
	flags = arch_encode_bit(flags, ptr_Z, Z_SHIFT, 32, bb);
	flags = arch_encode_bit(flags, ptr_C, C_SHIFT, 32, bb);
	flags = arch_encode_bit(flags, ptr_V, V_SHIFT, 32, bb);
	flags = arch_encode_bit(flags, ptr_I, I_SHIFT, 32, bb);

	return flags;
}

static void
arch_arm_flags_decode(Value *flags, BasicBlock *bb)
{
	arch_decode_bit(flags, ptr_N, N_SHIFT, 32, bb);
	arch_decode_bit(flags, ptr_Z, Z_SHIFT, 32, bb);
	arch_decode_bit(flags, ptr_C, C_SHIFT, 32, bb);
	arch_decode_bit(flags, ptr_V, V_SHIFT, 32, bb);
	arch_decode_bit(flags, ptr_I, I_SHIFT, 32, bb);
}

void
arch_arm_emit_decode_reg(cpu_t *cpu, BasicBlock *bb)
{
	// declare flags
	ptr_N = new AllocaInst(getIntegerType(1), "N", bb);
	ptr_Z = new AllocaInst(getIntegerType(1), "Z", bb);
	ptr_C = new AllocaInst(getIntegerType(1), "C", bb);
	ptr_V = new AllocaInst(getIntegerType(1), "V", bb);
	ptr_I = new AllocaInst(getIntegerType(1), "I", bb);

	// decode CPSR
	Value *flags = new LoadInst(ptr_CPSR, "", false, bb);
	arch_arm_flags_decode(flags, bb);
}

void
arch_arm_spill_reg_state(cpu_t *cpu, BasicBlock *bb)
{
	Value *flags = arch_arm_flags_encode(bb);
	new StoreInst(flags, ptr_CPSR, false, bb);
}
//printf("%s:%d PC=$%04X\n", __func__, __LINE__, pc);
//printf("%s:%d\n", __func__, __LINE__);
