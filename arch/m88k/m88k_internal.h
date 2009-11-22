#include "libcpu.h"
#include "libcpu_m88k.h"

int arch_m88k_tag_instr(cpu_t *cpu, addr_t pc, int *flow_type, addr_t *new_pc);
int arch_m88k_disasm_instr(cpu_t *cpu, addr_t pc, char *line, unsigned int max_line);
Value *arch_m88k_recompile_cond(cpu_t *cpu, addr_t pc, BasicBlock *bb);
int arch_m88k_recompile_instr(cpu_t *cpu, addr_t pc, BasicBlock *bb_dispatch, BasicBlock *bb, BasicBlock *bb_target, BasicBlock *bb_cond, BasicBlock *bb_next);

#define INSTR(a) RAM32(cpu->RAM, a)
