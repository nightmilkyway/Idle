#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#define ISOCTAL(c) ((c) >= '0' && (c) <= '7')
#define ISBINARY(c) ((c) >= '0' && (c) <= '1')
#define arraysize(a) (sizeof(a)/sizeof(a[0]))

#define IDLEASM_TOKENCOUNT 16
#define IDLEASM_TOKENSIZE 32
#define IDLEASM_TABLESIZE 256
#define IDLEASM_SVDCOUNT 4096
#define IDLEASM_LABELCOUNT 4096
#define IDLEASM_STCOUNT 1048576

//#define IDLEASM_BIGENDIAN 0
//#define IDLEASM_LITTLEENDIAN 1

#define idleasm_error(mac, msg) idleasm_logerr(mac, msg)

#define IDLEASM_TYPE_NULL 0
#define IDLEASM_TYPE_REG 1
#define IDLEASM_TYPE_IMM 2
#define IDLEASM_TYPE_FLOAT 3
#define IDLEASM_TYPE_IDENT 4

typedef enum idleasm_err {
	IDLEASM_ERR_SUCCESSFUL_EXIT = 0,
	IDLEASM_ERR_FAILED_EXIT,
	IDLEASM_ERR_LABEL_NAME_IS_NOT_IDENT,
	IDLEASM_ERR_INCORRECT_ARGUMENT,
	IDLEASM_ERR_INCORRECT_OPCODE,
	IDLEASM_ERR_INCORRECT_INSTRUCTION,
	IDLEASM_ERR_INTEGER_CONST_ISNT_VALID,
	IDLEASM_ERR_ALLOCATION_FAILED,
	IDLEASM_ERR_FILE_NOT_READ,
} idleasm_err;

void idleasm_logerr(int e, const char *msg) {
	fprintf(stderr, "[idleasm_err] %#.8x, %s\n", e, msg);
	exit(e);
}

typedef enum parser_token {
	IDLEASM_PARSER_UNKNOWN,
	IDLEASM_PARSER_TAG,
	IDLEASM_PARSER_OPC,
	IDLEASM_PARSER_REG,
	IDLEASM_PARSER_INTEGER,
	IDLEASM_PARSER_FLOAT,
	IDLEASM_PARSER_STRING,
	IDLEASM_PARSER_CHAR,
	IDLEASM_PARSER_LSQUAREBR,
	IDLEASM_PARSER_RSQUAREBR,
	IDLEASM_PARSER_LFIGUREBR,
	IDLEASM_PARSER_RFIGUREBR,
	IDLEASM_PARSER_LROUNDBR,
	IDLEASM_PARSER_RROUNDBR,
	IDLEASM_PARSER_PLUS,
	IDLEASM_PARSER_MINUS,
	IDLEASM_PARSER_STAR,
	IDLEASM_PARSER_SLASH,
	IDLEASM_PARSER_VERTICAL,
	IDLEASM_PARSER_CARRIAGE,
	IDLEASM_PARSER_AMPERSAND,
	IDLEASM_PARSER_TILDA,
	IDLEASM_PARSER_LSHIFT,
	IDLEASM_PARSER_RSHIFT,
	IDLEASM_PARSER_MODULE,
	IDLEASM_PARSER_DOLLAR,
	IDLEASM_PARSER_EQUAL,
	IDLEASM_PARSER_COMMA,
	IDLEASM_PARSER_DOT,
	IDLEASM_PARSER_SEMICOLON,
	IDLEASM_PARSER_COLON,
	IDLEASM_PARSER_IDENT,
} parser_token;

typedef struct opboard_t {
	char *name;
	int unary;
	int priority;
	parser_token t;
} opboard_t;

typedef struct labelstat_t {
	char *lb_name;
	uint64_t ln;
} labelstat_t;

typedef struct argtype_t {
	char *name;
	uint16_t op;
	uint8_t at0;
	uint8_t at1;
} argtype_t;

typedef struct nstat_t {
	char *mnemonic;
	uint8_t r;
} nstat_t;

typedef struct opsvd_t {
	uint16_t op;
	uint8_t arg0;
	uint8_t arg1;
	uint32_t imm;
} opsvd_t;

typedef struct idleprm_t {
	opsvd_t *svd;
	labelstat_t *lbl;
	unsigned isvd;
	unsigned iptr;
	unsigned mlp;
} idleprm_t;

const char *intr_name[65536] = {
	"exit\0", "abort\0", "readc\0", "writec\0", "loadsd\0", "loadad\0", "loadid\0", "writes", "reads", "writen", "readn", NULL
};

const opboard_t opbrd[] = {
	{"+\0", 0, 5, IDLEASM_PARSER_PLUS},
	{"-\0", 2, 5, IDLEASM_PARSER_MINUS},
	{"*\0", 0, 6, IDLEASM_PARSER_STAR},
	{"/\0", 0, 6, IDLEASM_PARSER_SLASH},
	{"%\0", 0, 6, IDLEASM_PARSER_MODULE},
	{"|\0", 0, 1, IDLEASM_PARSER_VERTICAL},
	{"^\0", 0, 2, IDLEASM_PARSER_CARRIAGE},
	{"&\0", 0, 3, IDLEASM_PARSER_AMPERSAND},
	{"~\0", 1, 7, IDLEASM_PARSER_TILDA},
	{"<<\0", 0, 4, IDLEASM_PARSER_LSHIFT},
	{">>\0", 0, 4, IDLEASM_PARSER_RSHIFT},
	{"(\0", 0, 0, IDLEASM_PARSER_LROUNDBR},
	{")\0", 0, 0, IDLEASM_PARSER_RROUNDBR}
};

const argtype_t mn[] = {
	{"hlt", 0, IDLEASM_TYPE_NULL, IDLEASM_TYPE_NULL},
	{"nop", 1, IDLEASM_TYPE_NULL, IDLEASM_TYPE_NULL},
	{"add", 2, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"add", 3, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"sub", 4, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"sub", 5, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"rsb", 6, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"rsb", 7, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"mul", 8, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"mul", 9, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"div", 10, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"div", 11, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"rdv", 12, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"rdv", 13, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"mod", 14, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"mod", 15, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"rmd", 16, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"rmd", 17, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"imul", 18, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"imul", 19, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"idiv", 20, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"idiv", 21, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"irdv", 22, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"irdv", 23, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"and", 24, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"and", 25, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"or", 26, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"or", 27, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"xor", 28, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"xor", 29, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"not", 30, IDLEASM_TYPE_REG, IDLEASM_TYPE_NULL},
	{"shr", 31, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"shr", 32, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"shl", 33, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"shl", 34, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"mov", 35, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"mov", 36, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"xchg", 37, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"cmp", 38, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"cmp", 39, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"jmp", 40, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"je", 41, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jl", 42, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jnge", 42, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jg", 43, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jnle", 43, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jle", 44, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jng", 44, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jge", 45, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jnl", 45, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"jne", 46, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"int", 47, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"push", 48, IDLEASM_TYPE_REG, IDLEASM_TYPE_NULL},
	{"pop", 49, IDLEASM_TYPE_REG, IDLEASM_TYPE_NULL},
	{"asr", 50, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"asr", 51, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"bt", 52, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"bt", 53, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"bts", 54, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"bts", 55, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"btr", 56, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"btr", 57, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"bti", 58, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"bti", 59, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"call", 60, IDLEASM_TYPE_IDENT, IDLEASM_TYPE_NULL},
	{"ret", 61, IDLEASM_TYPE_IMM, IDLEASM_TYPE_NULL},
	{"ldb", 62, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"ldb", 63, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"lddb", 64, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"lddb", 65, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"ldqb", 66, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"ldqb", 67, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"stb", 68, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"stb", 69, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"stdb", 70, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"stdb", 71, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"stqb", 72, IDLEASM_TYPE_REG, IDLEASM_TYPE_REG},
	{"stqb", 73, IDLEASM_TYPE_REG, IDLEASM_TYPE_IMM},
	{"id", 0xf001, IDLEASM_TYPE_IMM, IDLEASM_TYPE_NULL},
};

const nstat_t r[] = {
	{"atr0", 0x00}, {"atr1", 0x01}, {"rtv", 0x02}, {"rta", 0x03},
	{"rg0", 0x04}, {"rg1", 0x05}, {"rg2", 0x06}, {"rg3", 0x07},
	{"sp", 0x08}, {"rtaa", 0x09}, {"fp", 0x0a}, {"t0", 0x0b},
	{"t1", 0x0c}, {"t2", 0x0d}, {"t3", 0x0e}, {"t4", 0x0f},
	{"t5", 0x10}, {"t6", 0x11}, {"t7", 0x12}, {"t8", 0x13},
	{"t9", 0x14}, {"t10", 0x15}, {"t11", 0x16}, {"t12", 0x17},
	{"s0", 0x18}, {"s1", 0x19}, {"s2", 0x1a}, {"s3", 0x1b},
	{"s4", 0x1c}, {"s5", 0x1d}, {"s6", 0x1e}, {"s7", 0x1f},
	{"s8", 0x20}, {"s9", 0x21}, {"s10", 0x22}, {"s11", 0x23},
	{"p0", 0x24}, {"p1", 0x25}, {"p2", 0x26}, {"p3", 0x27},
	{"p4", 0x28}, {"p5", 0x29}, {"p6", 0x2a}, {"p7", 0x2b},
	{"xh", 0x2c}, {"xl", 0x2d}, {"yh", 0x2e}, {"yl", 0x2f},
	{"zh", 0x30}, {"zl", 0x31}, {"y50", 0x32}, {"y51", 0x33},
	{"y52", 0x34}, {"y53", 0x35}, {"y54", 0x36}, {"y55", 0x37},
	{"y56", 0x38}, {"y57", 0x39}, {"y58", 0x3a}, {"y59", 0x3b},
	{"y60", 0x3c}, {"y61", 0x3d}, {"y62", 0x3e}, {"y63", 0x3f},

	{"y0", 0x00}, {"y1", 0x01}, {"y2", 0x02}, {"y3", 0x03},
	{"y4", 0x04}, {"y5", 0x05}, {"y6", 0x06}, {"y7", 0x07},
	{"y8", 0x08}, {"y9", 0x09}, {"y10", 0x0a}, {"y11", 0x0b},
	{"y12", 0x0c}, {"y13", 0x0d}, {"y14", 0x0e}, {"y15", 0x0f},
	{"y16", 0x10}, {"y17", 0x11}, {"y18", 0x12}, {"y19", 0x13},
	{"y20", 0x14}, {"y21", 0x15}, {"y22", 0x16}, {"y23", 0x17},
	{"y24", 0x18}, {"y25", 0x19}, {"y26", 0x1a}, {"y27", 0x1b},
	{"y28", 0x1c}, {"y29", 0x1d}, {"y30", 0x1e}, {"y31", 0x1f},
	{"y32", 0x20}, {"y33", 0x21}, {"y34", 0x22}, {"y35", 0x23},
	{"y36", 0x24}, {"y37", 0x25}, {"y38", 0x26}, {"y39", 0x27},
	{"y40", 0x28}, {"y41", 0x29}, {"y42", 0x2a}, {"y43", 0x2b},
	{"y44", 0x2c}, {"y45", 0x2d}, {"y46", 0x2e}, {"y47", 0x2f},
	{"y48", 0x30}, {"y49", 0x31}, {"y50", 0x32}, {"y51", 0x33},
	{"y52", 0x34}, {"y53", 0x35}, {"y54", 0x36}, {"y55", 0x37},
	{"y56", 0x38}, {"y57", 0x39}, {"y58", 0x3a}, {"y59", 0x3b},
	{"y60", 0x3c}, {"y61", 0x3d}, {"y62", 0x3e}, {"y63", 0x3f}
};

typedef struct lexstat_t {
    char *token_matrix;
    unsigned token_count;
    parser_token *token_int;
    unsigned ipr;
    unsigned mlp;
} lexstat_t;
/*
int idleasm_endianness(void) {
	uint64_t w = UINT64_C(0x0000000000000001);
	uint8_t c = *((uint8_t *)&w);
	if(c) {
		return IDLEASM_LITTLEENDIAN;
	} else {
		return IDLEASM_BIGENDIAN;
	}
}

int idleasm_swap16(uint16_t *w) {
	uint16_t a = *w & UINT16_C(0xff00);
	uint16_t b = *w & UINT16_C(0x00ff);

	uint16_t r = (b << 8) | (a >> 8);

	*w = r;

	return 0;
}

int idleasm_swap32(uint32_t *w) {
	uint32_t a = *w & UINT32_C(0xff000000);
	uint32_t b = *w & UINT32_C(0x00ff0000);
	uint32_t c = *w & UINT32_C(0x0000ff00);
	uint32_t d = *w & UINT32_C(0x000000ff);

	uint32_t r = (d << 24) | (c << 8) | (b >> 8) | (a >> 24);

	*w = r;

	return 0;
}

int idleasm_swap64(uint64_t *w) {
	uint64_t a = *w & UINT64_C(0xff00000000000000);
	uint64_t b = *w & UINT64_C(0x00ff000000000000);
	uint64_t c = *w & UINT64_C(0x0000ff0000000000);
	uint64_t d = *w & UINT64_C(0x000000ff00000000);
	uint64_t e = *w & UINT64_C(0x00000000ff000000);
	uint64_t f = *w & UINT64_C(0x0000000000ff0000);
	uint64_t g = *w & UINT64_C(0x000000000000ff00);
	uint64_t h = *w & UINT64_C(0x00000000000000ff);

	uint64_t r = (a >> 56) | (b >> 40) | (c >> 24) | (d >> 8) | (e << 8) | (f << 24) | (g << 40) | (h << 56);

	*w = r;

	return 0;
}
*/
void idleasm_lexstat_alloc(lexstat_t *st) {
    st->token_count=0;
    st->ipr=0;
    st->mlp=2;
    st->token_matrix = calloc(IDLEASM_TOKENCOUNT, IDLEASM_TOKENSIZE);
    st->token_int = calloc(IDLEASM_TOKENCOUNT, sizeof(parser_token));
    if(!st->token_matrix) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
    if(!st->token_int) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
}

void idleasm_lexstat_initcopy(lexstat_t *dst, lexstat_t *src) {
	dst->token_count=src->token_count;
	dst->ipr=src->ipr;
	dst->mlp = src->mlp;
	dst->token_matrix = calloc(IDLEASM_TOKENCOUNT*(dst->mlp-1), IDLEASM_TOKENSIZE);
	dst->token_int = calloc(IDLEASM_TOKENCOUNT*(dst->mlp-1), sizeof(parser_token));

	memcpy(dst->token_matrix, src->token_matrix, IDLEASM_TOKENCOUNT*(dst->mlp-1)*IDLEASM_TOKENSIZE);
	memcpy(dst->token_int, src->token_int, IDLEASM_TOKENCOUNT*(dst->mlp-1)*sizeof(parser_token));

	if(!dst->token_matrix) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
    if(!dst->token_int) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}

}

void idleasm_lexstat_realloc(lexstat_t *st) {
    st->token_matrix = realloc(st->token_matrix, IDLEASM_TOKENCOUNT*IDLEASM_TOKENSIZE*st->mlp);
    st->token_int = realloc(st->token_matrix, IDLEASM_TOKENCOUNT*sizeof(parser_token)*st->mlp);

    if(!st->token_matrix) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
    if(!st->token_int) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
}

void idleasm_lexstat_free(lexstat_t *st) {
    free(st->token_matrix);
    free(st->token_int);
}

int idleasm_bintable_build(const char *ign, const char *del, const char *swap, const char *incl, char *table) {
    memset(table, 0, IDLEASM_TABLESIZE);
    for(int x = 0; ign[x] != 0; x++) {
        table[(unsigned char)ign[x]] = 1;
    }
    for(int x = 0; del[x] != 0; x++) {
        table[(unsigned char)del[x]] = 2;
    }
    for(int x = 0; swap[x] != 0; x++) {
        table[(unsigned char)swap[x]] = 3;
    }
    for(int x = 0; incl[x] != 0; x++) {
        table[(unsigned char)incl[x]] = 4;
    }
    return 0;
}

int idleasm_token(const char *inpt, const char *table, lexstat_t *st) {
	/*
		* IS = string pointer
		* ITX = matrix pointer by X
		* ITY = matrix pointer by Y
		* TG = swap mode
		* IC = include all mode
		* RSV = check if memory for token reserved
		* MLP = blocks count
	*/
    int is, itx=0, ity=0, tg=0, ic=0, rsv=1;

    /*
		* CASE 0: character is letter
		* CASE 1: character is ignorable
		* CASE 2: character is delimiter
		* CASE 3: character is swap character
		* CASE 4: character is including all character
    */

    for(is = 0; inpt[is] != 0; is++) {
        switch(table[(unsigned char)inpt[is]]) {
            case 0:
                if(ity >= IDLEASM_TOKENSIZE) {ity=0; itx+=1;} if(itx >= IDLEASM_TOKENCOUNT) {idleasm_lexstat_realloc(st);}
                if(ic) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; break;}
                if(tg) {
                    tg=0;
                    if(!rsv) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = 0; itx+=1; ity=0; rsv=1;}
                }

                st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; rsv=0;

                break;
            case 1:
                if(ity >= IDLEASM_TOKENSIZE) {ity=0; itx+=1;} if(itx >= IDLEASM_TOKENCOUNT) {idleasm_lexstat_realloc(st);}
                if(ic) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; break;}
                if(!rsv) {itx+=1; ity=0; rsv=1;}
                break;
            case 2:
                if(ity >= IDLEASM_TOKENSIZE) {ity=0; itx+=1;} if((itx+1) >= IDLEASM_TOKENCOUNT) {idleasm_lexstat_realloc(st);}
                if(ic) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; break;}
                if(!rsv) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = 0; itx+=1; ity=0;}
                st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++;
                st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = 0; itx+=1; ity=0; rsv=1;
                break;
            case 3:
                if(ity >= IDLEASM_TOKENSIZE) {ity=0; itx+=1;} if(itx >= IDLEASM_TOKENCOUNT) {idleasm_lexstat_realloc(st);}
                if(ic) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; break;}
                if(!tg) {
                    tg=1;
                    if(!rsv) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = 0; itx+=1; ity=0; rsv=1;}
                }

                st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; rsv=0;

                break;
            case 4:
            	if(is > 0 && inpt[is-1] == '\\') {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++; break;}
                if(ity >= IDLEASM_TOKENSIZE) {ity=0; itx+=1;} if(itx >= IDLEASM_TOKENCOUNT) {idleasm_lexstat_realloc(st);}
                st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = inpt[is]; ity++;
                if(ic) {st->token_matrix[(itx * IDLEASM_TOKENSIZE) + ity] = 0; itx+=1; ity=0; rsv=1;}
                ic = !ic;
        }
    }

    st->token_count = itx;

    return 0;
}

int idleasm_inttype(const char *src, int *minus) {
    int l = strlen(src)-1;

    if(l+1 == 0) {return 0x00;}

    *minus = 0;

	switch(src[l]) {
		case 'h':
			for(int i = 0; i < l; i++) {
				if(!isxdigit((int)src[i])) {return 0x00;}
			} return 0x02;
		case 'o': case 'q':
			for(int i = 0; i < l; i++) {
				if(!ISOCTAL((int)src[i])) {return 0x00;}
			} return 0x03;
			break;
		case 'b': case 'y':
			for(int i = 0; i < l; i++) {
				if(!ISBINARY((int)src[i])) {return 0x00;}
			} return 0x04;
			break;
		case 'd':
			for(int i = 0; i < l; i++) {
				if(!isdigit((int)src[i])) {return 0x00;}
			} return 0x05;
			break;
		default:
			switch(src[0]) {
				case '0':
					switch(src[1]) {
						case 'h': case 'x':
							for(int i = 2; i < (l+1); i++) {
								if(!isxdigit((int)src[i])) {return 0x00;}
							} return 0x12;
						case 'b': case 'y':
							for(int i = 2; i < (l+1); i++) {
								if(!ISBINARY((int)src[i])) {return 0x00;}
							} return 0x14;
							break;
						case 'd':
							for(int i = 2; i < (l+1); i++) {
								if(!isdigit((int)src[i])) {return 0x00;}
							} return 0x15;
							break;
						case 'o': case 'q':
							for(int i = 2; i < (l+1); i++) {
								if(!ISOCTAL((int)src[i])) {return 0x00;}
							} return 0x13;
							break;
						default:
							for(int i = 0; i < (l+1); i++) {
								if(!ISOCTAL((int)src[i])) {return 0x00;}
							} return 0x16;
					}
				case '-':
					*minus = 1;
					switch(src[1]) {
						case '0':
							switch(src[2]) {
								case 'h': case 'x':
									for(int i = 3; i < (l+1); i++) {
										if(!isxdigit((int)src[i])) {return 0x00;}
									} return 0x12;
								case 'b': case 'y':
									for(int i = 3; i < (l+1); i++) {
										if(!ISBINARY((int)src[i])) {return 0x00;}
									} return 0x14;
									break;
								case 'd':
									for(int i = 3; i < (l+1); i++) {
										if(!isdigit((int)src[i])) {return 0x00;}
									} return 0x15;
									break;
								case 'o': case 'q': default:
									for(int i = 3; i < (l+1); i++) {
										if(!ISOCTAL((int)src[i])) {return 0x00;}
									} return 0x13;
									break;
							}
						default:
							for(int i = 1; i < (l+1); i++) {
								if(!isdigit((int)src[i])) {return 0x00;}
							} return 0x01;
							break;
					}
					break;
				default:
					for(int i = 0; i < (l+1); i++) {
						if(!isdigit((int)src[i])) {return 0x00;}
					} return 0x01;
					break;
			}

    }
}

int idleasm_intconv(const char *s, uint64_t *i, unsigned n, uint64_t base, int sign) {
	*i = 0;
	if(n == 0) {return 1;}
	uint64_t m = 1;
	for(unsigned j = 0, k = (n - 1); j < n; j++, k--) {
		if(s[k] == 0) {return 0;}
		switch(s[k]) {
		case '0':
			*i += 0; m*=base; break;
		case '1':
			*i += m; m*=base; break;
		case '2':
			*i += 2*m; m*=base; break;
		case '3':
			*i += 3*m; m*=base; break;
		case '4':
			*i += 4*m; m*=base; break;
		case '5':
			*i += 5*m; m*=base; break;
		case '6':
			*i += 6*m; m*=base; break;
		case '7':
			*i += 7*m; m*=base; break;
		case '8':
			*i += 8*m; m*=base; break;
		case '9':
			*i += 9*m; m*=base; break;
		case 'A': case 'a':
			*i += 10*m; m*=base; break;
		case 'B': case 'b':
			*i += 11*m; m*=base; break;
		case 'C': case 'c':
			*i += 12*m; m*=base; break;
		case 'D': case 'd':
			*i += 13*m; m*=base; break;
		case 'E': case 'e':
			*i += 14*m; m*=base; break;
		case 'F': case 'f':
			*i += 15*m; m*=base; break;
		default:
			idleasm_error(IDLEASM_ERR_INTEGER_CONST_ISNT_VALID, "unknown digit in integer constant");
		}
	}

	if(sign) {*i = -(*i);}
	return 0;
}

int idleasm_intform(const char *s, uint64_t *i) {
	unsigned l = strlen(s); int q;
	switch(idleasm_inttype(s, &q)) {
	case 0x01:
		return idleasm_intconv(s, i, l, 10, q);
	case 0x02:
		return idleasm_intconv(s, i, l-1, 16, q);
	case 0x03:
		return idleasm_intconv(s, i, l-1, 8, q);
	case 0x04:
		return idleasm_intconv(s, i, l-1, 2, q);
	case 0x05:
		return idleasm_intconv(s, i, l-1, 10, q);
	case 0x12:
		return idleasm_intconv(&s[2], i, l-2, 16, q);
	case 0x13:
		return idleasm_intconv(&s[2], i, l-2, 8, q);
	case 0x14:
		return idleasm_intconv(&s[2], i, l-2, 2, q);
	case 0x15:
		return idleasm_intconv(&s[2], i, l-2, 10, q);
	case 0x16:
		return idleasm_intconv(s, i, l, 8, q);
	default:
		idleasm_error(IDLEASM_ERR_INTEGER_CONST_ISNT_VALID, "unknown integer constant format");
	}
	return 1;
}

int idleasm_build_finddata(unsigned *i, char *name, int arg0, int arg1) {
	int l0=0, l1=0, l2=0;
	for(unsigned x = 0; x < arraysize(mn); x++) {
		l0= !strcasecmp(name, mn[x].name);
		l1= arg0==mn[x].at0;
		l2= arg1==mn[x].at1;
		if(l0 & l1 & l2) {*i = x; return 0;}
	}
	return 1;
}

int isident_str(const char *s) {
	unsigned l = strlen(s);
	if(!((isalpha(s[0])) || (s[0] == '_'))) {
		return 0;
	}
	if(l > 1) {
		for(unsigned i = 1; s[i] != 0; i++) {
			if(!((isalnum(s[i])) || (s[i] == '_'))) {
				return 0;
			}
		}
	}
	return 1;
}

int isoperand_str(const char *s) {
	for(unsigned i = 0; i < arraysize(mn); i++) {
		if(!strcasecmp(s, mn[i].name)) {
			return 1;
		}
	}
	return 0;
}

int isregister_str(const char *s) {
	for(unsigned i = 0; i < arraysize(r); i++) {
		if(!strcasecmp(s, r[i].mnemonic)) {
			return 1;
		}
	}
	return 0;
}

int isstring_str(const char *s) {
	unsigned l = strlen(s);
	if(s[0] == '\"' && s[l-1] == '\"') {return 1;}
	return 0;
}

void idleasm_prmalloc(idleprm_t *prm) {
	prm->iptr = 0;
	prm->isvd = 0;
	prm->mlp = 2;
	prm->lbl = calloc(IDLEASM_LABELCOUNT, sizeof(labelstat_t));
	prm->svd = calloc(IDLEASM_SVDCOUNT, sizeof(opsvd_t));
	for(unsigned a = 0; a < IDLEASM_LABELCOUNT; a++) {
		prm->lbl[a].lb_name = malloc(IDLEASM_TOKENSIZE);
		if(!prm->lbl[a].lb_name) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
	}
	if(!prm->lbl) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
	if(!prm->svd) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
}

void idleasm_prmrealloc(idleprm_t *prm) {
	prm->lbl = realloc(prm->lbl, IDLEASM_LABELCOUNT*sizeof(labelstat_t)*prm->mlp);
	prm->svd = realloc(prm->svd, IDLEASM_SVDCOUNT*sizeof(opsvd_t)*prm->mlp);
	prm->mlp += 1;
	for(unsigned a = IDLEASM_LABELCOUNT*(prm->mlp-2); a < IDLEASM_LABELCOUNT*(prm->mlp-1); a++) {
		prm->lbl[a].lb_name = malloc(IDLEASM_TOKENSIZE);
		if(!prm->lbl[a].lb_name) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
	}
	if(!prm->lbl) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
	if(!prm->svd) {idleasm_error(IDLEASM_ERR_ALLOCATION_FAILED, "memory allocation failed");}
}

void idleasm_prmfree(idleprm_t *prm) {
	for(unsigned a = 0; a < IDLEASM_LABELCOUNT; a++) {
		free(prm->lbl[a].lb_name);
	}
	free(prm->lbl);
	free(prm->svd);
}

int idleasm_build_label(idleprm_t *prm, char *name, unsigned ln) {
	if(prm->iptr >= IDLEASM_LABELCOUNT*(prm->mlp-1)) {idleasm_prmrealloc(prm);}
	strncpy(prm->lbl[prm->iptr].lb_name, name, IDLEASM_TOKENSIZE);
	prm->lbl[prm->iptr++].ln = ln;
	return 0;
}

int idleasm_build_binary(idleprm_t *prm, char *mnemonic, int targ0, int targ1, uint8_t a0, uint8_t a1, uint32_t imm) {
	unsigned i;
	if(idleasm_build_finddata(&i, mnemonic, targ0, targ1)) {idleasm_error(IDLEASM_ERR_INCORRECT_INSTRUCTION, "invalid instruction");}
	if(prm->isvd >= IDLEASM_SVDCOUNT*(prm->mlp-1)) {idleasm_prmrealloc(prm);}
	prm->svd[prm->isvd].op = mn[i].op;
	prm->svd[prm->isvd].arg0 = a0;
	prm->svd[prm->isvd].arg1 = a1;
	prm->svd[prm->isvd].imm = imm;
	prm->isvd += 1;
	return 0;
}

int idleasm_id_directive(idleprm_t *prm, uint64_t w) {
	((uint64_t *)prm->svd)[prm->isvd] = w;
	prm->isvd += 1;
	return 0;
}

int idleasm_enuminstr(lexstat_t *st, unsigned *i) {
	int q = 0, y = 0; uint64_t num = 0; unsigned S = *i;
	if((st->token_count - S) < 2) {return 0;}
	if(isoperand_str(&st->token_matrix[(*i)*IDLEASM_TOKENSIZE])) {
		st->token_int[S] = IDLEASM_PARSER_OPC;
		if(!strcmp(&st->token_matrix[(S+1)*IDLEASM_TOKENSIZE], ";\0")) {st->token_int[S+1] = IDLEASM_PARSER_SEMICOLON; return 0;}
		if((st->token_count - (S + 1)) % 2) {
			idleasm_error(IDLEASM_ERR_INCORRECT_INSTRUCTION, "incorrect instruction");
		}
		for(unsigned k = S + 1; k < st->token_count; k+=2) {
			if(isregister_str(&st->token_matrix[k*IDLEASM_TOKENSIZE])) {
				st->token_int[k] = IDLEASM_PARSER_REG;
			}
			else if(isstring_str(&st->token_matrix[k*IDLEASM_TOKENSIZE])) {
				st->token_int[k] = IDLEASM_PARSER_STRING;
			}
			else if(isident_str(&st->token_matrix[k*IDLEASM_TOKENSIZE])) {
				st->token_int[k] = IDLEASM_PARSER_IDENT;
			}
			else if(idleasm_inttype(&st->token_matrix[k*IDLEASM_TOKENSIZE], &q)) {
				st->token_int[k] = IDLEASM_PARSER_INTEGER;
			}
			else {
				idleasm_error(IDLEASM_ERR_INCORRECT_ARGUMENT, "unknown type of argument");
			}
			if(!strcmp(&st->token_matrix[(k+1)*IDLEASM_TOKENSIZE], ";\0")) {st->token_int[k+1] = IDLEASM_PARSER_SEMICOLON; break;}
			if(!strcmp(&st->token_matrix[(k+1)*IDLEASM_TOKENSIZE], ",\0")) {st->token_int[k+1] = IDLEASM_PARSER_COMMA;}
			else {idleasm_error(IDLEASM_ERR_INCORRECT_INSTRUCTION, "unknown separator");}
		}
	}
	return 0;
}

int idleasm_enumtag(lexstat_t *st, unsigned *i) {
	if(st->token_count < 2) {idleasm_error(IDLEASM_ERR_INCORRECT_INSTRUCTION, "incorrect instruction");}
	if(isident_str(&st->token_matrix[0]) && !strcmp(&st->token_matrix[IDLEASM_TOKENSIZE], ":\0")) {
		st->token_int[0] = IDLEASM_PARSER_TAG; st->token_int[1] = IDLEASM_PARSER_COLON; *i = 2; return 0;
	}
	if(!strcmp(&st->token_matrix[IDLEASM_TOKENSIZE], ":\0")) {idleasm_error(IDLEASM_ERR_LABEL_NAME_IS_NOT_IDENT, "label name is not identifier");}
	return 0;
}

int idleasm_enumerator(lexstat_t *st) {
	unsigned i = 0;
	idleasm_enumtag(st, &i);
	idleasm_enuminstr(st, &i);
	return 0;
}

unsigned idleasm_ett(unsigned i) {
	switch(i) {
	case IDLEASM_PARSER_FLOAT:
		return IDLEASM_TYPE_FLOAT;
	case IDLEASM_PARSER_INTEGER:
		return IDLEASM_TYPE_IMM;
	case IDLEASM_PARSER_REG:
		return IDLEASM_TYPE_REG;
	case IDLEASM_PARSER_IDENT:
		return IDLEASM_TYPE_IDENT;
	default:
		return IDLEASM_TYPE_NULL;
	}
}

unsigned idleasm_getopc(lexstat_t *st) {
	return st->token_int[0] == IDLEASM_PARSER_TAG ? 2 : 0;
}

int idleasm_getarg(lexstat_t *st, int *arg0, int *arg1) {
	unsigned i = idleasm_getopc(st);
	if(st->token_count >= (i + 4)) {
		*arg0 = idleasm_ett(st->token_int[i + 1]);
		*arg1 = idleasm_ett(st->token_int[i + 3]);
	} else if(st->token_count >= (i + 2)) {
		*arg0 = idleasm_ett(st->token_int[i + 1]);
		*arg1 = IDLEASM_TYPE_NULL;
	} else {
		*arg0 = IDLEASM_TYPE_NULL;
		*arg1 = IDLEASM_TYPE_NULL;
	}
	return 0;
}

int idleasm_push_label(lexstat_t *st, idleprm_t *prm, unsigned ln) {
	if(idleasm_getopc(st) == 2) {
		idleasm_build_label(prm, &st->token_matrix[0], ln);
	}
	return 0;
}

int idleasm_findlabel(const char *name, idleprm_t *prm, uint32_t *n) {
	unsigned sz = IDLEASM_LABELCOUNT * (prm->mlp - 1);
	for(unsigned i = 0; i < sz; i++) {
		if(!strcmp(name, prm->lbl[i].lb_name)) {
			*n = prm->lbl[i].ln; return 1;
		}
	}
	return 0;
}

int idleasm_findreg(const char *name, uint8_t *n) {
	for(unsigned i = 0; i < arraysize(r); i++) {
		if(!strcasecmp(name, r[i].mnemonic)) {
			*n = r[i].r; return 1;
		}
	}
	return 0;
}

int idleasm_findintr(const char *name, uint32_t *n) {
	for(unsigned i = 0; intr_name[i] != NULL; i++) {
		if(!strcmp(name, intr_name[i])) {
			*n = i; return 1;
		}
	}
	return 0;
}

unsigned idleasm_jmpissue(lexstat_t *st, idleprm_t *prm, uint32_t *n) {
	unsigned l = (idleasm_getopc(st) + 1) * IDLEASM_TOKENSIZE;
	int r = idleasm_findlabel(&st->token_matrix[l], prm, n);
	*n = (*n) - prm->isvd - 1;
	return r;
}

int idleasm_push_instr(lexstat_t *st, idleprm_t *prm) {
	unsigned oa = 0; int ta0 = 0, ta1 = 0;
	uint8_t a0 = 0, a1 = 0; uint32_t imm = 0;
	uint64_t tmp;
	if(!strcmp(&st->token_matrix[IDLEASM_TOKENSIZE], ":\0") && st->token_count == 2) {idleasm_build_binary(prm, "nop\0", IDLEASM_TYPE_NULL, IDLEASM_TYPE_NULL, 0, 0, 0); return 0;}
	oa = idleasm_getopc(st);
	idleasm_getarg(st, &ta0, &ta1);
	if(!strcmp(&st->token_matrix[oa * IDLEASM_TOKENSIZE], "id\0") && (ta0 == IDLEASM_TYPE_IMM && ta1 == IDLEASM_TYPE_NULL)) {
		idleasm_intform(&st->token_matrix[(oa+1) * IDLEASM_TOKENSIZE], &tmp);
		idleasm_id_directive(prm, tmp);
		return 0;
	}
	if(ta0 == IDLEASM_TYPE_IDENT && !strcmp(&st->token_matrix[oa*IDLEASM_TOKENSIZE], "int\0")) {
		idleasm_findintr(&st->token_matrix[(oa+1) * IDLEASM_TOKENSIZE], &imm);
		goto nj;
	}
	else if(ta1 == IDLEASM_TYPE_IDENT && !strcmp(&st->token_matrix[oa*IDLEASM_TOKENSIZE], "int\0")) {
		idleasm_findintr(&st->token_matrix[(oa+3) * IDLEASM_TOKENSIZE], &imm);
		goto nj;
	} else {}
	if(ta0 == IDLEASM_TYPE_IDENT) {
		idleasm_jmpissue(st, prm, &imm);
	}
	else if(ta1 == IDLEASM_TYPE_IDENT) {
		idleasm_jmpissue(st, prm, &imm);
	} else {}
	nj:
	if(ta0 == IDLEASM_TYPE_IMM) {
		idleasm_intform(&st->token_matrix[(oa+1) * IDLEASM_TOKENSIZE], &tmp);
		imm = (uint32_t)((int32_t)((int64_t)tmp));
	}
	else if(ta1 == IDLEASM_TYPE_IMM) {
		idleasm_intform(&st->token_matrix[(oa+3) * IDLEASM_TOKENSIZE], &tmp);
		imm = (uint32_t)((int32_t)((int64_t)tmp));
	} else {}
	if(ta0 == IDLEASM_TYPE_FLOAT) {
		idleasm_error(IDLEASM_ERR_FAILED_EXIT, "unreleased feature");
	}
	if(ta1 == IDLEASM_TYPE_FLOAT) {
		idleasm_error(IDLEASM_ERR_FAILED_EXIT, "unreleased feature");
	}
	if(ta0 == IDLEASM_TYPE_REG) {
		idleasm_findreg(&st->token_matrix[(oa+1) * IDLEASM_TOKENSIZE], &a0);
	}
	if(ta1 == IDLEASM_TYPE_REG) {
		idleasm_findreg(&st->token_matrix[(oa+3) * IDLEASM_TOKENSIZE], &a1);
	}
	idleasm_build_binary(prm, &st->token_matrix[oa * IDLEASM_TOKENSIZE], ta0, ta1, a0, a1, imm);
	return 0;
}

int idleasm_main(int argc, char **argv) {
	if(argc < 3) {return 1;}

	FILE *ff = fopen(argv[1], "r");
	FILE *fo = fopen(argv[2], "wb");

	if(fo == NULL || ff == NULL) {idleasm_error(IDLEASM_ERR_FILE_NOT_READ, "failed to read file");}

	char buf[512];

	lexstat_t *st = calloc(IDLEASM_STCOUNT, sizeof(lexstat_t)); idleprm_t prm; char table[256];

	idleasm_prmalloc(&prm);

	idleasm_bintable_build("\r\v\t\n ", "()[]{},:;", "+*-/%^&|~", "\"\'`", table);

	char *p;

	unsigned c;

	for(c = 0; c < IDLEASM_STCOUNT; c++) {
		p = fgets(buf, 512, ff);

		idleasm_lexstat_alloc(&st[c]);

		idleasm_token(buf, table, &st[c]);

		idleasm_enumerator(&st[c]);

		idleasm_push_label(&st[c], &prm, c);

		if(p == NULL) {break;}
	}

	for(unsigned x = 0; x < c; x++) {
		idleasm_push_instr(&st[x], &prm);
	}

	for(unsigned x = 0; x < c; x++) {
		fwrite(&prm.svd[x], sizeof(opsvd_t), 1, fo);
	}

	idleasm_prmfree(&prm);

	for(unsigned i = 0; i < c; i++) {
		idleasm_lexstat_free(&st[i]);
	}

	free(st);

	return 0;
}

int main(int argc, char **argv) {
	idleasm_main(argc, argv);

	return 0;
}

