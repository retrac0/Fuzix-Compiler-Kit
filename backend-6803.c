/*
 *	The 680x/630x series processors are closely related except for the
 *	6809/6309 which is totally different and to us unrelated. All of them
 *	have a very small number of registers and an instruction set that is
 *	built around register/memory operations.
 *
 *	The original 6800 cannot push/pop the index register and has no 16bit
 *	maths operations. It is not considerded in this target at this point
 *
 *	The 6803 has a single index register and index relative addressing.
 *	There is almost no maths on the index register except an 8bit add.
 *	The processor also lacks the ability to do use the stack as an index
 *	but can copy the stack pointer to x in one instruction. Thus we have
 *	to play tracking games on the contents of X.
 *
 *	The 6303 is slightly pipelined and adds some bit operations that are
 *	mostly not relative to the compiler as well as a the ability to exchange
 *	X and D, which makes maths on the index much easier.
 *
 *	The 68HC11 adds an additional Y index register, which has a small
 *	performance penalty. For now we ignore Y and treat it as a 6803.
 *
 *	All conditional branches are short range, the assemnbler supports
 *	jxx which turns into the correct instruction combination for a longer
 *	conditional branch as needed.
 *
 *	Almsot every instruction changes the flags. This helps us hugely in
 *	most areas and is a total nightmare in a couple.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "compiler.h"
#include "backend.h"

#define BYTE(x)		(((unsigned)(x)) & 0xFF)
#define WORD(x)		(((unsigned)(x)) & 0xFFFF)

/*
 *	State for the current function
 */
static unsigned frame_len;	/* Number of bytes of stack frame */
static unsigned sp;		/* Stack pointer offset tracking */

/*
 *	Register state tracking
 */

static unsigned value_d;
static unsigned value_x;
static unsigned state_d;
static unsigned state_x;

#define INVALID		0
#define	CONSTANT	1
#define	BCONSTANT	2	/* Only used with D tracker */
#define STACKREL	3	/* Only used with X tracker */

static void invalidate_d(void)
{
	state_d = INVALID;
}

static void load_d(unsigned v, unsigned keepc)
{
	unsigned char vl = v;
	unsigned char vh = v >> 8;
	unsigned char vdl = value_d;
	unsigned char vdh = value_d >> 8;

	/* Complete match */
	if (state_d == CONSTANT && value_d == v)
		return;

	/* We know the low byte and it matches */
	if ((state_d == CONSTANT || state_d == BCONSTANT) && vdl == vl) {
		if (vh || keepc)
			printf("\tldaa #%d\n", vh);
		else
			puts("\tclra");
		state_d = CONSTANT;
		value_d = v;
		return;
	}
	/* See if the high byte matches */
	if (state_d == CONSTANT && vh == vdh) {
		if (vl || keepc)
			printf("\tldab #%d\n", vl);
		else
			puts("\tclrb\n");
		state_d = CONSTANT;
		value_d = v;
		return;
	}
	state_d = CONSTANT;
	value_d = v;
	if (v == 0 && !keepc) {
		puts("\tclra\n\tclrb");
		return;
	}
	printf("ldd #%d\n", v);
}

static void load_b(unsigned v, unsigned keepc)
{
	if ((state_d == BCONSTANT || state_d == CONSTANT) && (value_d & 0xFF) == (v & 0xFF))
		return;
	if (state_d == CONSTANT) {
		value_d &= 0xFF00;
		value_d |= v & 0xFF;
	} else {
		state_d = BCONSTANT;
		value_d = v;
	}
	if (v == 0 && !keepc)
		puts("\tclrb");
	else
		printf("\tldab #%d\n", v);
}

static void modify_d(unsigned v)
{
	if (v == 0)
		return;
	if (state_d != CONSTANT) {
		printf("\taddd #%d\n", v);
		return;
	};
	v += value_d;
	/* Let the load routine figure out how to load this most efficiently */
	load_d(v, 0);
}

/* For now just track stack relative X. We might want to track symbol relative
   as well, but probably not constant. Due to the way X works we don't generate
   code in these helpers but instead in the code that uses them */

static void invalidate_x(void)
{
	state_x = INVALID;
}

static void load_x_s(void)
{
	state_x = STACKREL;
	value_x = sp;
}

static void modify_x(unsigned v)
{
	if (state_x != STACKREL)
		return;
	value_x += v;
}

static void repeated_instruction(const char *x, unsigned n)
{
	while(n--)
		puts(x);
}


/*
 *	Helpers
 */

/* Generate an X that can access offset spoff from the stack. Most of the
   time we are able to just load X as needed and keeping using n,X. O a bigger
   stack we have to play around with X a bit */

static unsigned gen_offset(int spoff, int spmax, unsigned save_d)
{
	if (state_x != STACKREL) {
		load_x_s();
		puts("\ttsx");
	}
	/* X is now some offset relative to what we need, but may be too big */
	spoff += value_x;
	if (spoff <= spmax)
		return spoff;
	/* The value is out of range of addressing to be used. Modify X */
	if (cpu == 6303 || cpu == 6811) {
		if (spmax - spoff < 5) {
			repeated_instruction("\tinx", spmax - spoff);
			modify_x(spmax - spoff);
		} else {
			puts("\txchg");
			printf("\taddd #%d\n", spoff);
			puts("\txchg");
			modify_x(spoff);
		}
		return 0;
	}
	if (spoff < 0)
		error("badgo");
	/* We only have ABX and we may have to save B */
	if (spmax -spoff < 10 - 2 * save_d)
		repeated_instruction("\tinx", spmax - spoff);
	else {
		unsigned n = spmax - spoff;
		/* Could in theory optimize if state of d is known and happens
		   to contain right byte - unlikely! */
		if (save_d)
			puts("\tstab @tmp");
		if (n >= 255) {
			puts("\tldab #255");
			while(n >= 255) {
				puts("\tabx");
				n -= 255;
			}
		}
		if (n)
			printf("\tldab #%d\n\tabx\n", n);
		if (save_d)
			puts("\tldab @tmp");
	}
	modify_x(spmax - spoff);
	return spmax;
}

static void shrink_stack(unsigned v)
{
	/* TODO: set properly */
	invalidate_x();
	if (v < 12) {
		while(v > 1) {
			puts("\tpulx");
			v -= 2;
		}
		if (v)
			puts("\tins");
	} else {
		if (cpu == 6303 || cpu == 6811) {
			puts("\ttsx\n\txchg");	/* SP into D */
			printf("\taddd #%d\n", v);
			puts("\txchg\n\ttxs");
		} else {
			puts("\tstab @tmp");
			if (v >= 255) {
			puts("\tldab #255");
				while(v >= 255) {
					puts("\tabx");
					v -= 255;
				}
			}
			switch(v) {
			case 1:
				puts("\tins");
				break;
			case 2:
				puts("\tpulx");
				break;
			default:
				printf("\tldab #%d\n\tabx\n", v);
				break;
			}
			puts("\tldab @tmp\n");
		}
	}
}

/* We assume this can always kill d */
static void grow_stack(unsigned v)
{
	unsigned cost = 12;
	if (cpu == 6803)
		cost = 22;
	if (v < cost) {
		while(v > 1) {
			puts("\tpshx");
			v -= 2;
		}
		if (v)
			puts("\tdes");
	} else {
		if (cpu == 6303 || cpu == 6811) {
			invalidate_x();
			puts("\ttsx\n\txchg");	/* SP into D */
			printf("\taddd #%d\n", -v);
			puts("\txchg\n\ttxs");
		} else {
			invalidate_d();
			puts("\tsts @tmp");
			printf("\tldd #%d\n", -v);
			puts("\taddd @tmp");
			puts("\tstd @tmp");
			puts("\tlds @tmp");
		}
	}
}
/*
 *	Our chance to do tree rewriting. We don't do much for the 8080
 *	at this point, but we do rewrite name references and function calls
 *	to make them easier to process.
 */
struct node *gen_rewrite_node(struct node *n)
{
	return n;
}

/* Export the C symbol */
void gen_export(const char *name)
{
	printf("	.export _%s\n", name);
}

void gen_segment(unsigned s)
{
	switch(s) {
	case A_CODE:
		printf("\t.text\n");
		break;
	case A_DATA:
		printf("\t.data\n");
		break;
	case A_LITERAL:
		printf("\t.literal\n");
		break;
	case A_BSS:
		printf("\t.bss\n");
		break;
	default:
		error("gseg");
	}
}

void gen_prologue(const char *name)
{
	printf("_%s:\n", name);
	invalidate_d();
}

/* Generate the stack frame */
void gen_frame(unsigned size)
{
	frame_len = size;
	grow_stack(size);
}

void gen_epilogue(unsigned size)
{
	if (sp != size) {
		error("sp");
	}
	sp -= size;
	shrink_stack(size);
	puts("\trts");
}

void gen_label(const char *tail, unsigned n)
{
	printf("L%d%s:\n", n, tail);
	invalidate_d();
	invalidate_x();
}

void gen_jump(const char *tail, unsigned n)
{
	printf("\tjmp L%d%s\n", n, tail);
}

void gen_jfalse(const char *tail, unsigned n)
{
	printf("\tjz L%d%s\n", n, tail);
}

void gen_jtrue(const char *tail, unsigned n)
{
	printf("\tjnz L%d%s\n", n, tail);
}

void gen_switch(unsigned n, unsigned type)
{
	gen_helpcall(NULL);
	printf("switch");
	helper_type(type, 0);
	printf("\n\t.word Sw%d\n", n);
}

void gen_switchdata(unsigned n, unsigned size)
{
	printf("Sw%d:\n", n);
	printf("\t.word %d\n", size);
}

void gen_case(unsigned tag, unsigned entry)
{
	invalidate_d();
	invalidate_x();
	printf("Sw%d_%d:\n", tag, entry);
}

void gen_case_label(unsigned tag, unsigned entry)
{
	invalidate_d();
	invalidate_x();
	printf("Sw%d_%d:\n", tag, entry);
}

void gen_case_data(unsigned tag, unsigned entry)
{
	printf("\t.word Sw%d_%d\n", tag, entry);
}

void gen_helpcall(struct node *n)
{
	invalidate_d();
	invalidate_x();
	printf("\thjsr __");
}

void gen_helpclean(struct node *n)
{
}

void gen_data_label(const char *name, unsigned align)
{
	printf("_%s:\n", name);
}

void gen_space(unsigned value)
{
	printf("\t.ds %d\n", value);
}

void gen_text_data(unsigned n)
{
	printf("\t.word T%d\n", n);
}

void gen_literal(unsigned n)
{
	if (n)
		printf("T%d:\n", n);
}

void gen_name(struct node *n)
{
	printf("\t.word _%s+%d\n", namestr(n->snum), WORD(n->value));
}

void gen_value(unsigned type, unsigned long value)
{
	if (PTR(type)) {
		printf("\t.word %u\n", (unsigned) value);
		return;
	}
	switch (type) {
	case CCHAR:
	case UCHAR:
		printf("\t.byte %u\n", (unsigned) value & 0xFF);
		break;
	case CSHORT:
	case USHORT:
		printf("\t.word %d\n", (unsigned) value & 0xFFFF);
		break;
	case CLONG:
	case ULONG:
	case FLOAT:
		/* We are big endian */
		printf("\t.word %d\n", (unsigned) ((value >> 16) & 0xFFFF));
		printf("\t.word %d\n", (unsigned) (value & 0xFFFF));
		break;
	default:
		error("unsuported type");
	}
}

void gen_start(void)
{
	printf("\t.code\n");
}

void gen_end(void)
{
}

void gen_tree(struct node *n)
{
	codegen_lr(n);
	printf(";\n");
}

/*
 *	All our sizes are fairly predictable. Our stack is byte oriented
 *	so we push bytes as bytes.
 */
static unsigned get_size(unsigned t)
{
	if (PTR(t))
		return 2;
	if (t == CSHORT || t == USHORT)
		return 2;
	if (t == CCHAR || t == UCHAR)
		return 1;
	if (t == CLONG || t == ULONG || t == FLOAT)
		return 4;
	if (t == CLONGLONG || t == ULONGLONG || t == DOUBLE)
		return 8;
	if (t == VOID)
		return 0;
	error("gs");
	return 0;
}

static unsigned get_stack_size(unsigned t)
{
	return get_size(t);
}

unsigned gen_push(struct node *n)
{
	/* Our push will put the object on the stack, so account for it */
	unsigned v = get_stack_size(n->type);
	sp += v;
	if (v > 4)
		return 0;
	puts("\tpshb");
	if (v > 1)
		puts("\tpsha");
	if (v > 2)
		printf("\tldx @sreg\n\tpshx\n");
	return 1;
}

/*
 *	If possible turn this node into a direct access. We've already checked
 *	that the right hand side is suitable. If this returns 0 it will instead
 *	fall back to doing it stack based.
 */
unsigned gen_direct(struct node *n)
{
	unsigned v;
	switch(n->op) {
	/* Clean up is special and must be handled directly. It also has the
	   type of the function return so don't use that for the cleanup value
	   in n->right */
	case T_CLEANUP:
		shrink_stack(n->right->value);
		return 1;	
	}
	return 0;
}

/*
 *	Allow the code generator to shortcut the generation of the argument
 *	of a single argument operator (for example to shortcut constant cases
 *	or simple name loads that can be done better directly)
 */
unsigned gen_uni_direct(struct node *n)
{
	return 0;
}

/*
 *	Allow the code generator to shortcut trees it knows
 */
unsigned gen_shortcut(struct node *n)
{
	if (n->op == T_COMMA) {
		n->left->flags |= NORETURN;
		codegen_lr(n->left);
		codegen_lr(n->right);
		return 1;
	}
	return 0;
}

unsigned gen_node(struct node *n)
{
	unsigned s = get_size(n->type);
	unsigned v = WORD(n->value);
	unsigned h = WORD(n->value >> 16);
	/* Function call arguments are special - they are removed by the
	   act of call/return and reported via T_CLEANUP */
	if (n->left && n->op != T_ARGCOMMA && n->op != T_FUNCCALL)
		sp -= get_stack_size(n->left->type);
	switch(n->op) {
	case T_CONSTANT:
		switch(s) {
		case 4:
			load_d(h, 0);
			printf("\tstd @hireg\n");
		case 2:
			load_d(v, 0);
			return 1;
		case 1:
			load_b(v, 0);
			return 1;
		}
		break;
	case T_NAME:
		invalidate_d();
		printf("\tldd #%s+%d\n", namestr(n->snum), v);
		return 1;
	case T_LABEL:
		invalidate_d();
		printf("\tldd #T%d+%d\n", n->val2, v);
		return 1;
	}
	return 0;
}
