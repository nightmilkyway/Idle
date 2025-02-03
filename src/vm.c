/*
Copyright 2025 nightmilkyway

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>

#define IDLE_REGS_COUNT 64
#define IDLE_DEFAULTSTACK 0x6000
#define IDLE_FILESIZE 0x100000
#define IDLE_VMINTSIZE 65536
#define IDLE_RADRESS_COUNT 1024
#define IDLE_RAWDATASIZE 65536

#define arraysize(a) (sizeof(a)/sizeof(a[0]))

#define idle_error(v, mac) idlevm_logerr(v, mac, #mac)

#define idle_fmtregs "[idle_dbg] %12s=0x%.16llx%12s=0x%.16llx\n[idle_dbg] %12s=0x%.16llx%12s=0x%.16llx\n"

#define BIT(a, i) ((a >> (i & 0x3f)) & 0x1)
#define BITSET(a, i) (a | (1 << (i & 0x3f)))
#define BITINVERT(a, i) (a ^ (1 << (i & 0x3f)))
#define BITRESET(a, i) (a & ~(1 << (i & 0x3f)))

typedef enum idlevm_err {
	IDLEVM_ERR_SUCCESSFUL_EXIT = 0,
	IDLEVM_ERR_INCORRECT_OPCODE,
	IDLEVM_ERR_INCORRECT_ARGUMENT,
	IDLEVM_ERR_ILLEGAL_MEMORY_ACCESS,
	IDLEVM_ERR_ALLOCATION_FAILED,
	IDLEVM_ERR_DIVIDE_BY_ZERO,
	IDLEVM_ERR_NULL_DEREFERENCE,
	IDLEVM_ERR_FILE_NOT_READ,
	IDLEVM_ERR_STACK_OVERFLOW,
	IDLEVM_ERR_STACK_UNDERFLOW,
	IDLEVM_ERR_ADRESS_STACK_OVERFLOW,
	IDLEVM_ERR_ADRESS_STACK_UNDERFLOW,
	IDLEVM_ERR_INCORRECT_INT_NUMBER,
} idlevm_err;

typedef enum idlevm_op {
	HLT=0, NOP, ADD_R, ADD_I, SUB_R, SUB_I, RSB_R, RSB_I, MUL_R, MUL_I, DIV_R, DIV_I, RDV_R, RDV_I, MOD_R, MOD_I, RMD_R, RMD_I, IMUL_R, IMUL_I, IDIV_R,
	IDIV_I, IRDV_R, IRDV_I, AND_R, AND_I, OR_R, OR_I, XOR_R, XOR_I, NOT_R, SHR_R, SHR_I, SHL_R, SHL_I, MOV_R, MOV_I, XCHG, CMP_R, CMP_I, JMP, JE, JL, JG, JLE,
	JGE, JNE, INT, PUSH, POP, ASR_R, ASR_I, BT_R, BT_I, BTS_R, BTS_I, BTR_R, BTR_I, BTI_R, BTI_I, CALL, RET, LDB_R, LDB_I, LDDB_R, LDDB_I, LDQB_R, LDQB_I,
	STB_R, STB_I, STDB_R, STDB_I, STQB_R, STQB_I
} idlevm_op;

typedef struct idlevm_command {
	uint16_t op;
	uint8_t arg1;
	uint8_t arg2;
	uint32_t imm;
} idlevm_command;

typedef struct idle_vm {
	uint64_t regs[IDLE_REGS_COUNT];
	uint64_t radress[IDLE_RADRESS_COUNT];
	uint8_t *raw_data;
	uint64_t *stack;
	uint64_t mp;
} idle_vm;

typedef int (*idlevm_func)(idle_vm *v, idlevm_command *cm);

int idlevmint_exit(idle_vm *v, idlevm_command *cm) {
	exit(v->regs[4]);
}

int idlevmint_abort(idle_vm *v, idlevm_command *cm) {
	abort();
}

int idlevmint_readc(idle_vm *v, idlevm_command *cm) {
	v->regs[2] = getc(stdin); return 0;
}

int idlevmint_writec(idle_vm *v, idlevm_command *cm) {
	putc(v->regs[4], stdout); return 0;
}

int idlevmint_vmloadstack(idle_vm *v, idlevm_command *cm) {
	v->regs[2] = (uint64_t)v->stack[v->regs[4]]; return 0;
}

int idlevmint_vmloadastack(idle_vm *v, idlevm_command *cm) {
	v->regs[2] = (uint64_t)v->radress[v->regs[4]]; return 0;
}

int idlevmint_vmloaddata(idle_vm *v, idlevm_command *cm) {
	v->regs[2] = ((uint64_t *)cm)[v->regs[4]]; return 0;
}

int idlevmint_writes(idle_vm *v, idlevm_command *cm) {
	fputs((char *)&v->raw_data[v->regs[4]], stdout); return 0;
}

int idlevmint_reads(idle_vm *v, idlevm_command *cm) {
	fgets((char *)&v->raw_data[v->regs[4]], v->regs[5], stdin); return 0;
}

int idlevmint_writen(idle_vm *v, idlevm_command *cm) {
	fprintf(stdout, "%lli", v->regs[4]); return 0;
}

int idlevmint_readn(idle_vm *v, idlevm_command *cm) {
	fscanf(stdin, "%lli", &v->regs[2]); return 0;
}

static const idlevm_func idle_vmint[IDLE_VMINTSIZE] = {
	idlevmint_exit,
	idlevmint_abort,
	idlevmint_readc,
	idlevmint_writec,
	idlevmint_vmloadstack,
	idlevmint_vmloadastack,
	idlevmint_vmloaddata,
	idlevmint_writes,
	idlevmint_reads,
	idlevmint_writen,
	idlevmint_readn
};

uint64_t clockCycleCount()
{
	unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

void idlevm_logerr(idle_vm *v, int e, const char *msg) {
	fprintf(stderr, "[idle_err] %#.8x, %s\n", e, msg);
	exit(e);
}

void idlevm_help() {
	;
}

void idlevm_logregs(idle_vm *v) {
	fprintf(stdout, "[idle_dbg] REGISTERS TABLE\n");
	fprintf(stdout, idle_fmtregs, "\"y0 .atr0\"", v->regs[0], "\"y1 .atr1\"", v->regs[1], "\"y2 .rtv\"", v->regs[2], "\"y3 .rta\"", v->regs[3]);
	fprintf(stdout, idle_fmtregs, "\"y4 .rg0\"", v->regs[4], "\"y5 .rg1\"", v->regs[5], "\"y6 .rg2\"", v->regs[6], "\"y7 .rg3\"", v->regs[7]);
	fprintf(stdout, idle_fmtregs, "\"y8 .sp\"", v->regs[8], "\"y9 .rtaa\"", v->regs[9], "\"y10 .fp\"", v->regs[10], "\"y11 .t0\"", v->regs[11]);
	fprintf(stdout, idle_fmtregs, "\"y12 .t1\"", v->regs[12], "\"y13 .t2\"", v->regs[13], "\"y14 .t3\"", v->regs[14], "\"y15 .t4\"", v->regs[15]);
	fprintf(stdout, idle_fmtregs, "\"y16 .t5\"", v->regs[16], "\"y17 .t6\"", v->regs[17], "\"y18 .t7\"", v->regs[18], "\"y19 .t8\"", v->regs[19]);
	fprintf(stdout, idle_fmtregs, "\"y20 .t9\"", v->regs[20], "\"y21 .t10\"", v->regs[21], "\"y22 .t11\"", v->regs[22], "\"y23 .t12\"", v->regs[23]);
	fprintf(stdout, idle_fmtregs, "\"y24 .s0\"", v->regs[24], "\"y25 .s1\"", v->regs[25], "\"y26 .s2\"", v->regs[26], "\"y27 .s3\"", v->regs[27]);
	fprintf(stdout, idle_fmtregs, "\"y28 .s4\"", v->regs[28], "\"y29 .s5\"", v->regs[29], "\"y30 .s6\"", v->regs[30], "\"y31 .s7\"", v->regs[31]);
	fprintf(stdout, idle_fmtregs, "\"y32 .s8\"", v->regs[32], "\"y33 .s9\"", v->regs[33], "\"y34 .s10\"", v->regs[34], "\"y35 .s11\"", v->regs[35]);
	fprintf(stdout, idle_fmtregs, "\"y36 .p0\"", v->regs[36], "\"y37 .p1\"", v->regs[37], "\"y38 .p2\"", v->regs[38], "\"y39 .p3\"", v->regs[39]);
	fprintf(stdout, idle_fmtregs, "\"y40 .p4\"", v->regs[40], "\"y41 .p5\"", v->regs[41], "\"y42 .p6\"", v->regs[42], "\"y43 .p7\"", v->regs[43]);
	fprintf(stdout, idle_fmtregs, "\"y44 .\"", v->regs[44], "\"y45 .\"", v->regs[45], "\"y46 .\"", v->regs[46], "\"y47 .\"", v->regs[47]);
	fprintf(stdout, idle_fmtregs, "\"y48 .\"", v->regs[48], "\"y49 .\"", v->regs[49], "\"y50 .\"", v->regs[50], "\"y51 .\"", v->regs[51]);
	fprintf(stdout, idle_fmtregs, "\"y52 .\"", v->regs[52], "\"y53 .\"", v->regs[53], "\"y54 .\"", v->regs[54], "\"y55 .\"", v->regs[55]);
	fprintf(stdout, idle_fmtregs, "\"y56 .\"", v->regs[56], "\"y57 .\"", v->regs[57], "\"y58 .\"", v->regs[58], "\"y59 .\"", v->regs[59]);
	fprintf(stdout, idle_fmtregs, "\"y60 .\"", v->regs[60], "\"y61 .\"", v->regs[61], "\"y62 .\"", v->regs[62], "\"y63 .\"", v->regs[63]);
}

void idlevm_init(idle_vm *v) {
	memset(v->regs, 0, sizeof(uint64_t) * IDLE_REGS_COUNT);
	memset(v->radress, 0, sizeof(uint64_t) * IDLE_RADRESS_COUNT);
	v->stack = (uint64_t *) calloc(IDLE_DEFAULTSTACK, sizeof(uint64_t));
	v->raw_data = (uint8_t *) calloc(IDLE_RAWDATASIZE, sizeof(uint8_t));
	if(v->stack == NULL) {idle_error(v, IDLEVM_ERR_ALLOCATION_FAILED);}
	if(v->raw_data == NULL) {idle_error(v, IDLEVM_ERR_ALLOCATION_FAILED);}
	v->mp = 2;
}

void idlevm_expandst(idle_vm *v) {
	v->stack = realloc(v->stack, v->mp*IDLE_DEFAULTSTACK*sizeof(uint64_t)); v->mp+=1;
	if(v->stack == NULL) {idle_error(v, IDLEVM_ERR_ALLOCATION_FAILED);}
}

void idlevm_free(idle_vm *v) {
	free(v->stack);
	free(v->raw_data);
}

int idlevm_run(idle_vm *v, idlevm_command *cm, size_t n) {
	uint64_t t, t1; int32_t tj;
	uint64_t *areg = v->regs; uint64_t *astack = v->stack; idlevm_command acm;
	uint64_t *arad = v->radress;
	uint8_t *araw = v->raw_data;
	uint64_t arg1r, arg2r;
	uint64_t ip; //uint64_t k=0;
	for(ip = 0; ip < n; ip++) {
		acm = cm[ip];
		arg1r = acm.arg1;
		arg2r = acm.arg2;
		//uint64_t s = clockCycleCount();
		switch(acm.op) {
		case HLT:
			//printf("%llu\n", k);
			return 0;
		case NOP:
			break;
		case JMP:
			tj = (int32_t)acm.imm;
			ip += (int64_t)tj;
			break;
		case JE:
			ip += (int64_t)((int32_t)acm.imm) & (!!(areg[0] & 0x01) - 1);
			break;
		case JL:
			ip += (int64_t)((int32_t)acm.imm) & (!!(areg[0] & 0x04) - 1);
			break;
		case JG:
			ip += (int64_t)((int32_t)acm.imm) & (!!(areg[0] & 0x02) - 1);
			break;
		case JLE:
			ip += (int64_t)((int32_t)acm.imm) & (!!(areg[0] & 0x05) - 1);
			break;
		case JGE:
			ip += (int64_t)((int32_t)acm.imm) & (!!(areg[0] & 0x03) - 1);
			break;
		case JNE:
			ip += (int64_t)((int32_t)acm.imm) & (!!(areg[0] & 0x06) - 1);
			break;
		case ADD_R:
			areg[arg1r] = areg[arg1r] + areg[arg2r];
			break;
		case ADD_I:
			areg[arg1r] = areg[arg1r] + acm.imm;
			break;
		case SUB_R:
			areg[arg1r] = areg[arg1r] - areg[arg2r];
			break;
		case SUB_I:
			areg[arg1r] = areg[arg1r] - acm.imm;
			break;
		case RSB_R:
			areg[arg1r] = areg[arg2r] - areg[arg1r];
			break;
		case RSB_I:
			areg[arg1r] = acm.imm - areg[arg1r];
			break;
		case MUL_R:
			areg[arg1r] = areg[arg1r] * areg[arg2r];
			break;
		case MUL_I:
			areg[arg1r] = areg[arg1r] * acm.imm;
			break;
		case DIV_R:
			if(!areg[arg2r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = areg[arg1r] / areg[arg2r];
			break;
		case DIV_I:
			if(!acm.imm) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = areg[arg1r] / acm.imm;
			break;
		case RDV_R:
			if(!areg[arg1r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = areg[arg2r] / areg[arg1r];
			break;
		case RDV_I:
			if(!areg[arg1r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = acm.imm / areg[arg1r];
			break;
		case MOD_R:
			if(!areg[arg2r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = areg[arg1r] % areg[arg2r];
			break;
		case MOD_I:
			if(!acm.imm) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = areg[arg1r] % acm.imm;
			break;
		case RMD_R:
			if(!areg[arg1r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = areg[arg2r] % areg[arg1r];
			break;
		case RMD_I:
			if(!areg[arg1r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = acm.imm % areg[arg1r];
			break;
		case IMUL_R:
			areg[arg1r] = (int64_t)areg[arg1r] * (int64_t)areg[arg2r];
			break;
		case IMUL_I:
			areg[arg1r] = (int64_t)areg[arg1r] * (int64_t)acm.imm;
			break;
		case IDIV_R:
			if(!areg[arg2r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = (int64_t)areg[arg1r] / (int64_t)areg[arg2r];
			break;
		case IDIV_I:
			if(!acm.imm) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = (int64_t)areg[arg1r] / (int64_t)acm.imm;
			break;
		case IRDV_R:
			if(!areg[arg1r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = (int64_t)areg[arg2r] / (int64_t)areg[arg1r];
			break;
		case IRDV_I:
			if(!areg[arg1r]) {idle_error(v, IDLEVM_ERR_DIVIDE_BY_ZERO);}
			areg[arg1r] = (int64_t)acm.imm / (int64_t)areg[arg1r];
			break;
		case AND_R:
			areg[arg1r] = areg[arg1r] & areg[arg2r];
			break;
		case AND_I:
			areg[arg1r] = areg[arg1r] & acm.imm;
			break;
		case OR_R:
			areg[arg1r] = areg[arg1r] | areg[arg2r];
			break;
		case OR_I:
			areg[arg1r] = areg[arg1r] | acm.imm;
			break;
		case XOR_R:
			areg[arg1r] = areg[arg1r] ^ areg[arg2r];
			break;
		case XOR_I:
			areg[arg1r] = areg[arg1r] ^ acm.imm;
			break;
		case NOT_R:
			areg[arg1r] = ~areg[arg1r];
			break;
		case SHL_R:
			areg[arg1r] = areg[arg1r] << areg[arg2r];
			break;
		case SHL_I:
			areg[arg1r] = areg[arg1r] << acm.imm;
			break;
		case SHR_R:
			areg[arg1r] = areg[arg1r] >> areg[arg2r];
			break;
		case SHR_I:
			areg[arg1r] = areg[arg1r] >> acm.imm;
			break;
		case ASR_R:
			areg[arg1r] = (int64_t)areg[arg1r] >> (int64_t)areg[arg2r];
			break;
		case ASR_I:
			areg[arg1r] = (int64_t)areg[arg1r] >> (int64_t)acm.imm;
			break;
		case MOV_R:
			areg[arg1r] = areg[arg2r]; break;
		case MOV_I:
			areg[arg1r] = acm.imm; break;
		case CMP_R:
			t = areg[arg1r];
			t1 = areg[arg2r];
			areg[0] = t > t1 ? 0x2 : (t < t1 ? 0x4 : 0x1);
			break;
		case CMP_I:
			t = areg[arg1r];
			t1 = acm.imm;
			areg[0] = t > t1 ? 0x2 : (t < t1 ? 0x4 : 0x1);
			break;
		case XCHG:
			t = areg[arg1r];
			areg[arg1r] = areg[arg2r];
			areg[arg2r] = t;
			break;
		case PUSH:
			astack[areg[8]++] = areg[arg1r]; break;
		case POP:
			areg[arg1r] = astack[--areg[8]]; break;
		case INT:
			if(acm.imm > arraysize(idle_vmint)) {idle_error(v, IDLEVM_ERR_INCORRECT_INT_NUMBER);}
			idle_vmint[acm.imm](v, cm); break;
		case BT_R:
			areg[arg1r] = BIT(areg[arg1r], areg[arg2r]); break;
		case BT_I:
			areg[arg1r] = BIT(areg[arg1r], acm.imm); break;
		case BTS_R:
			areg[arg1r] = BITSET(areg[arg1r], areg[arg2r]); break;
		case BTS_I:
			areg[arg1r] = BITSET(areg[arg1r], acm.imm); break;
		case BTR_R:
			areg[arg1r] = BITRESET(areg[arg1r], areg[arg2r]); break;
		case BTR_I:
			areg[arg1r] = BITRESET(areg[arg1r], acm.imm); break;
		case BTI_R:
			areg[arg1r] = BITINVERT(areg[arg1r], areg[arg2r]); break;
		case BTI_I:
			areg[arg1r] = BITINVERT(areg[arg1r], acm.imm); break;
		case CALL:
			if(areg[3] >= IDLE_RADRESS_COUNT) {idle_error(v, IDLEVM_ERR_ADRESS_STACK_OVERFLOW);}
			arad[areg[3]++] = ip;
			tj = (int32_t)acm.imm;
			ip = (int64_t)tj;
			break;
		case RET:
			if(!areg[3]) {idle_error(v, IDLEVM_ERR_ADRESS_STACK_UNDERFLOW);}
			ip = arad[--areg[3]];
			break;
		case LDB_R:
			areg[arg1r] = (uint64_t)araw[areg[arg2r]];
			break;
		case LDB_I:
			areg[arg1r] = (uint64_t)araw[acm.imm];
			break;
		case LDDB_R:
			areg[arg1r] = (uint64_t)((uint16_t *)araw)[areg[arg2r]];
			break;
		case LDDB_I:
			areg[arg1r] = (uint64_t)((uint16_t *)araw)[acm.imm];
			break;
		case LDQB_R:
			areg[arg1r] = (uint64_t)((uint32_t *)araw)[areg[arg2r]];
			break;
		case LDQB_I:
			areg[arg1r] = (uint64_t)((uint32_t *)araw)[acm.imm];
			break;
		case STB_R:
			araw[areg[arg2r]] = (uint8_t)areg[arg1r];
			break;
		case STB_I:
			araw[acm.imm] = (uint8_t)areg[arg1r];
			break;
		case STDB_R:
			((uint16_t *)araw)[areg[arg2r]] = (uint16_t)areg[arg1r];
			break;
		case STDB_I:
			((uint16_t *)araw)[acm.imm] = (uint16_t)areg[arg1r];
			break;
		case STQB_R:
			((uint32_t *)araw)[areg[arg2r]] = (uint32_t)areg[arg1r];
			break;
		case STQB_I:
			((uint32_t *)araw)[acm.imm] = (uint32_t)areg[arg1r];
			break;
		default:
			idle_error(v, IDLEVM_ERR_INCORRECT_OPCODE);
		}
		//uint64_t e = clockCycleCount();
		//printf("%i: %llu\n", acm.op, (e-s));
	}
	//printf("%llu\n", k);
	return 0;
}
/*
int main() {
	idle_vm v;
	idlevm_init(&v);

	idlevm_logregs(&v);
	return 0;
}
*/

int main(int argc, char **argv) {
	if(argc < 2) {return 0;}

	FILE *ff = fopen(argv[1], "rb");

	idlevm_command *cm = calloc(IDLE_FILESIZE, sizeof(idlevm_command));

	size_t n = fread(cm, sizeof(idlevm_command), IDLE_FILESIZE, ff);

	idle_vm v;

	idlevm_init(&v);

	if(!cm) {idle_error(&v, IDLEVM_ERR_ALLOCATION_FAILED);}
	if(!n) {idle_error(&v, IDLEVM_ERR_FILE_NOT_READ);}

	//uint64_t s = clockCycleCount();
	idlevm_run(&v, cm, n);
	//uint64_t e = clockCycleCount();

	//printf("%llu\n", (e-s));

	//idlevm_logregs(&v);

	idlevm_free(&v);

	free(cm);

	fclose(ff);

	return 0;
}
