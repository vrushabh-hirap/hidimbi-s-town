#ifndef SCRIPTORIUM_VM_H
#define SCRIPTORIUM_VM_H

// ---------------------------------------------------------------------------
// The Scriptorium VM (Stage 14) — a tiny 32-bit stack/register bytecode machine
// carved into the basement of the Dimbapura scriptorium.
//
// The machine has:
//   - 8 general purpose 32-bit registers R0 .. R7
//   - a 32-entry 32-bit operand stack
//   - a byte-addressed program counter with signed 16-bit relative branches
//   - an opaque 32-bit `gate_word` supplied by the host; the privileged
//     UNLOCK opcode traps unless R0 already holds it, which forces a
//     real program (one that reads the recovered seed) to be submitted.
//
// Instructions are variable length. The operand layouts are fixed:
//   0x00 NOP
//   0x01 PUSH imm32                       imm: 4 bytes LE
//   0x02 POP  r                           r  : 1 byte
//   0x10 LOAD r, off32                    r  : 1, off: 4 bytes LE
//   0x11 MOV  rd, ra                      rd : 1, ra: 1
//   0x12 ADD  rd, ra, rb                  rd : 1, ra: 1, rb: 1
//   0x13 SUB  rd, ra, rb
//   0x14 MUL  rd, ra, rb
//   0x15 DIV  rd, ra, rb                  (traps on divide by zero)
//   0x16 MOD  rd, ra, rb                  (traps on divide by zero)
//   0x17 XOR  rd, ra, rb
//   0x18 AND  rd, ra, rb
//   0x19 OR   rd, ra, rb
//   0x1A SHL  rd, ra, imm                 rd : 1, ra: 1, imm: 1
//   0x1B SHR  rd, ra, imm
//   0x1C ROL  rd, ra, imm
//   0x1D ROR  rd, ra, imm
//   0x20 CMPEQ rd, ra, rb                 rd <- (ra == rb) ? 1 : 0
//   0x21 CMPLT rd, ra, rb                 rd <- (ra <  rb) ? 1 : 0   (unsigned)
//   0x30 JMP  rel16                       rel: 2 bytes signed LE, from end of insn
//   0x31 JZ   r, rel16                    r   : 1, rel: 2 bytes signed LE
//   0x32 JNZ  r, rel16
//   0x40 UNLOCK                           traps unless R0 == gate_word
//   0xFF HALT                             stops and publishes R0..R3
//
// The HALT result word-vector [R0, R1, R2, R3] is the `scriptorium_mark`.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define VM_STEP_LIMIT 2000000

typedef struct {
    uint32_t r[8];
    uint32_t stack[32];
    int sp;
    uint32_t gate_word;
    int unlocked;
} TownVM;

// Returns 0 on success, negative on trap. On success out[0..3] = R0..R3.
inline int scriptorium_run(const uint8_t* prog, size_t prog_len,
                           const uint8_t* seed, size_t seed_len,
                           uint32_t gate_word, uint32_t out[4],
                           char* err, size_t err_cap) {
    TownVM vm;
    memset(&vm, 0, sizeof(vm));
    vm.gate_word = gate_word;
    vm.sp = 0;
    vm.unlocked = 0;

    size_t pc = 0;
    uint64_t steps = 0;

#define VM_FAIL(msg) do { \
        if (err && err_cap) snprintf(err, err_cap, "%s (pc=%zu)", msg, pc); \
        return -1; \
    } while (0)

    for (;;) {
        if (pc >= prog_len) VM_FAIL("TRAP_PC_OUT_OF_RANGE");
        if (++steps > VM_STEP_LIMIT) VM_FAIL("TRAP_STEP_LIMIT");

        uint8_t op = prog[pc];
        size_t opnd = pc + 1;

        switch (op) {
            case 0x00: { pc += 1; break; }

            case 0x01: { // PUSH imm32
                if (opnd + 4 > prog_len) VM_FAIL("TRAP_TRUNCATED_PUSH");
                uint32_t imm = (uint32_t)prog[opnd] | ((uint32_t)prog[opnd + 1] << 8) |
                               ((uint32_t)prog[opnd + 2] << 16) | ((uint32_t)prog[opnd + 3] << 24);
                if (vm.sp >= 32) VM_FAIL("TRAP_STACK_OVERFLOW");
                vm.stack[vm.sp++] = imm;
                pc = opnd + 4;
                break;
            }

            case 0x02: { // POP r
                if (opnd + 1 > prog_len) VM_FAIL("TRAP_TRUNCATED_POP");
                uint8_t r = prog[opnd];
                if (r > 7) VM_FAIL("TRAP_BAD_REGISTER");
                if (vm.sp <= 0) VM_FAIL("TRAP_STACK_UNDERFLOW");
                vm.r[r] = vm.stack[--vm.sp];
                pc = opnd + 1;
                break;
            }

            case 0x10: { // LOAD_SEED r, off32
                if (opnd + 5 > prog_len) VM_FAIL("TRAP_TRUNCATED_LOAD");
                uint8_t r = prog[opnd];
                uint32_t off = (uint32_t)prog[opnd + 1] | ((uint32_t)prog[opnd + 2] << 8) |
                               ((uint32_t)prog[opnd + 3] << 16) | ((uint32_t)prog[opnd + 4] << 24);
                if (r > 7) VM_FAIL("TRAP_BAD_REGISTER");
                if (off + 4 > seed_len) VM_FAIL("TRAP_SEED_OOB");
                vm.r[r] = (uint32_t)seed[off] | ((uint32_t)seed[off + 1] << 8) |
                          ((uint32_t)seed[off + 2] << 16) | ((uint32_t)seed[off + 3] << 24);
                pc = opnd + 5;
                break;
            }

#define VM_ALU3(expr) do { \
                if (opnd + 3 > prog_len) VM_FAIL("TRAP_TRUNCATED_ALU"); \
                uint8_t rd = prog[opnd], ra = prog[opnd + 1], rb = prog[opnd + 2]; \
                if (rd > 7 || ra > 7 || rb > 7) VM_FAIL("TRAP_BAD_REGISTER"); \
                vm.r[rd] = (uint32_t)(expr); \
                pc = opnd + 3; \
            } while (0)

            case 0x11: VM_ALU3(vm.r[ra]); break;                                    // MOV
            case 0x12: VM_ALU3(vm.r[ra] + vm.r[rb]); break;                          // ADD
            case 0x13: VM_ALU3(vm.r[ra] - vm.r[rb]); break;                          // SUB
            case 0x14: VM_ALU3(vm.r[ra] * vm.r[rb]); break;                          // MUL
            case 0x17: VM_ALU3(vm.r[ra] ^ vm.r[rb]); break;                          // XOR
            case 0x18: VM_ALU3(vm.r[ra] & vm.r[rb]); break;                          // AND
            case 0x19: VM_ALU3(vm.r[ra] | vm.r[rb]); break;                          // OR
            case 0x20: VM_ALU3(vm.r[ra] == vm.r[rb] ? 1u : 0u); break;               // CMPEQ
            case 0x21: VM_ALU3(vm.r[ra] <  vm.r[rb] ? 1u : 0u); break;               // CMPLT

            case 0x15: { // DIV
                if (opnd + 3 > prog_len) VM_FAIL("TRAP_TRUNCATED_DIV");
                uint8_t rd = prog[opnd], ra = prog[opnd + 1], rb = prog[opnd + 2];
                if (rd > 7 || ra > 7 || rb > 7) VM_FAIL("TRAP_BAD_REGISTER");
                if (vm.r[rb] == 0) VM_FAIL("TRAP_DIV_ZERO");
                vm.r[rd] = vm.r[ra] / vm.r[rb];
                pc = opnd + 3;
                break;
            }

            case 0x16: { // MOD
                if (opnd + 3 > prog_len) VM_FAIL("TRAP_TRUNCATED_MOD");
                uint8_t rd = prog[opnd], ra = prog[opnd + 1], rb = prog[opnd + 2];
                if (rd > 7 || ra > 7 || rb > 7) VM_FAIL("TRAP_BAD_REGISTER");
                if (vm.r[rb] == 0) VM_FAIL("TRAP_DIV_ZERO");
                vm.r[rd] = vm.r[ra] % vm.r[rb];
                pc = opnd + 3;
                break;
            }

#define VM_SHIFT(expr) do { \
                if (opnd + 3 > prog_len) VM_FAIL("TRAP_TRUNCATED_SHIFT"); \
                uint8_t rd = prog[opnd], ra = prog[opnd + 1], imm = prog[opnd + 2]; \
                if (rd > 7 || ra > 7) VM_FAIL("TRAP_BAD_REGISTER"); \
                vm.r[rd] = (uint32_t)(expr); \
                pc = opnd + 3; \
            } while (0)

            case 0x1A: VM_SHIFT(((imm) & 31) ? ((uint32_t)vm.r[ra] << ((imm) & 31)) : (uint32_t)vm.r[ra]); break;
            case 0x1B: VM_SHIFT(((imm) & 31) ? ((uint32_t)vm.r[ra] >> ((imm) & 31)) : (uint32_t)vm.r[ra]); break;
            case 0x1C: VM_SHIFT(((imm) & 31) ? (((uint32_t)vm.r[ra] << ((imm) & 31)) |
                                                ((uint32_t)vm.r[ra] >> (32 - ((imm) & 31))))
                                              : (uint32_t)vm.r[ra]); break;
            case 0x1D: VM_SHIFT(((imm) & 31) ? (((uint32_t)vm.r[ra] >> ((imm) & 31)) |
                                                ((uint32_t)vm.r[ra] << (32 - ((imm) & 31))))
                                              : (uint32_t)vm.r[ra]); break;

            case 0x30: { // JMP rel16
                if (opnd + 2 > prog_len) VM_FAIL("TRAP_TRUNCATED_JMP");
                int16_t rel = (int16_t)((uint16_t)prog[opnd] | ((uint16_t)prog[opnd + 1] << 8));
                size_t target = (size_t)((int64_t)(opnd + 2) + rel);
                if ((int64_t)target < 0) VM_FAIL("TRAP_BAD_BRANCH");
                pc = target;
                break;
            }

            case 0x31: { // JZ r, rel16
                if (opnd + 3 > prog_len) VM_FAIL("TRAP_TRUNCATED_JZ");
                uint8_t r = prog[opnd];
                int16_t rel = (int16_t)((uint16_t)prog[opnd + 1] | ((uint16_t)prog[opnd + 2] << 8));
                if (r > 7) VM_FAIL("TRAP_BAD_REGISTER");
                if (vm.r[r] == 0) {
                    size_t target = (size_t)((int64_t)(opnd + 3) + rel);
                    if ((int64_t)target < 0) VM_FAIL("TRAP_BAD_BRANCH");
                    pc = target;
                } else {
                    pc = opnd + 3;
                }
                break;
            }

            case 0x32: { // JNZ r, rel16
                if (opnd + 4 > prog_len) VM_FAIL("TRAP_TRUNCATED_JNZ");
                uint8_t r = prog[opnd];
                int16_t rel = (int16_t)((uint16_t)prog[opnd + 1] | ((uint16_t)prog[opnd + 2] << 8));
                if (r > 7) VM_FAIL("TRAP_BAD_REGISTER");
                if (vm.r[r] != 0) {
                    size_t target = (size_t)((int64_t)(opnd + 3) + rel);
                    if ((int64_t)target < 0) VM_FAIL("TRAP_BAD_BRANCH");
                    pc = target;
                } else {
                    pc = opnd + 3;
                }
                break;
            }

            case 0x40: { // UNLOCK
                if (vm.r[0] != vm.gate_word) VM_FAIL("TRAP_UNLOCK_REFUSED");
                vm.unlocked = 1;
                pc += 1;
                break;
            }

            case 0xFF: { // HALT
                out[0] = vm.r[0];
                out[1] = vm.r[1];
                out[2] = vm.r[2];
                out[3] = vm.r[3];
                return 0;
            }

            default:
                VM_FAIL("TRAP_BAD_OPCODE");
        }
    }
#undef VM_FAIL
}

#endif // SCRIPTORIUM_VM_H
