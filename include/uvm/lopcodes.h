/*
** $Id: lopcodes.h,v 1.148 2014/10/25 11:50:46 roberto Exp $
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include "uvm/llimits.h"


/*===========================================================================
  We assume that instructions are unsigned numbers.
  All instructions have an opcode in the first 6 bits.
  Instructions can have the following fields:
  'A' : 8 bits
  'B' : 9 bits
  'C' : 9 bits
  'Ax' : 26 bits ('A', 'B', and 'C' together)
  'Bx' : 18 bits ('B' and 'C' together)
  'sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.
  ===========================================================================*/


enum OpMode { iABC, iABx, iAsBx, iAx };  /* basic instruction format */


/*
** size and position of opcode arguments.
*/
#define SIZE_C		9
#define SIZE_B		9
#define SIZE_Bx		(SIZE_C + SIZE_B)
#define SIZE_A		8
#define SIZE_Ax		(SIZE_C + SIZE_B + SIZE_A)

#define SIZE_OP		6

#define POS_OP		0
#define POS_A		(POS_OP + SIZE_OP)
#define POS_C		(POS_A + SIZE_A)
#define POS_B		(POS_C + SIZE_C)
#define POS_Bx		POS_C
#define POS_Ax		POS_A


/*
** limits for opcode arguments.
** we use (signed) int to manipulate most arguments,
** so they must fit in LUAI_BITSINT-1 bits (-1 for sign)
*/
#if SIZE_Bx < LUAI_BITSINT-1
#define MAXARG_Bx        ((1<<SIZE_Bx)-1)
#define MAXARG_sBx        (MAXARG_Bx>>1)         /* 'sBx' is signed */
#else
#define MAXARG_Bx        MAX_INT
#define MAXARG_sBx        MAX_INT
#endif

#if SIZE_Ax < LUAI_BITSINT-1
#define MAXARG_Ax	((1<<SIZE_Ax)-1)
#else
#define MAXARG_Ax	MAX_INT
#endif


#define MAXARG_A        ((1<<SIZE_A)-1)
#define MAXARG_B        ((1<<SIZE_B)-1)
#define MAXARG_C        ((1<<SIZE_C)-1)


/* creates a mask with 'n' 1 bits at position 'p' */
#define MASK1(n,p)	((~((~(Instruction)0)<<(n)))<<(p))

/* creates a mask with 'n' 0 bits at position 'p' */
#define MASK0(n,p)	(~MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/

#define GET_OPCODE(i)	(lua_cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0)))
#define SET_OPCODE(i,o)	((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
		((lua_cast(Instruction, o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))

#define getarg(i,pos,size)	(lua_cast(int, ((i)>>pos) & MASK1(size,0)))
#define setarg(i,v,pos,size)	((i) = (((i)&MASK0(size,pos)) | \
                ((lua_cast(Instruction, v)<<pos)&MASK1(size,pos))))

#define GETARG_A(i)	getarg(i, POS_A, SIZE_A)
#define SETARG_A(i,v)	setarg(i, v, POS_A, SIZE_A)

#define GETARG_B(i)	getarg(i, POS_B, SIZE_B)
#define SETARG_B(i,v)	setarg(i, v, POS_B, SIZE_B)

#define GETARG_C(i)	getarg(i, POS_C, SIZE_C)
#define SETARG_C(i,v)	setarg(i, v, POS_C, SIZE_C)

#define GETARG_Bx(i)	getarg(i, POS_Bx, SIZE_Bx)
#define SETARG_Bx(i,v)	setarg(i, v, POS_Bx, SIZE_Bx)

#define GETARG_Ax(i)	getarg(i, POS_Ax, SIZE_Ax)
#define SETARG_Ax(i,v)	setarg(i, v, POS_Ax, SIZE_Ax)

#define GETARG_sBx(i)	(GETARG_Bx(i)-MAXARG_sBx)
#define SETARG_sBx(i,b)	SETARG_Bx((i),lua_cast(unsigned int, (b)+MAXARG_sBx))


#define CREATE_ABC(o,a,b,c)	((lua_cast(Instruction, o)<<POS_OP) \
			| (lua_cast(Instruction, a)<<POS_A) \
			| (lua_cast(Instruction, b)<<POS_B) \
			| (lua_cast(Instruction, c)<<POS_C))

#define CREATE_ABx(o,a,bc)	((lua_cast(Instruction, o)<<POS_OP) \
			| (lua_cast(Instruction, a)<<POS_A) \
			| (lua_cast(Instruction, bc)<<POS_Bx))

#define CREATE_Ax(o,a)		((lua_cast(Instruction, o)<<POS_OP) \
			| (lua_cast(Instruction, a)<<POS_Ax))


/*
** Macros to operate RK indices
*/

/* this bit 1 means constant (0 means register) */
#define BITRK		(1 << (SIZE_B - 1))

/* test whether value is a constant */
#define ISK(x)		((x) & BITRK)

/* gets the index of the constant */
#define INDEXK(r)	((int)(r) & ~BITRK)

#define MAXINDEXRK	(BITRK - 1)

/* code a constant index as a RK value */
#define RKASK(x)	((x) | BITRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG		MAXARG_A


/*
** R(x) - register
** Kst(x) - constant (in constant table)
** RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x)
*/


/*
** grep "ORDER OP" if you change these enums
*/

typedef enum {
    /*----------------------------------------------------------------------
    name		args	description
    ------------------------------------------------------------------------*/
    UOP_MOVE = 0, /*	A B	R(A) := R(B)					*/
	UOP_LOADK,/*	A Bx	R(A) := Kst(Bx)					*/
	UOP_LOADKX,/*	A 	R(A) := Kst(extra arg)				*/
	UOP_LOADBOOL,/*	A B C	R(A) := (Bool)B; if (C) pc++			*/
	UOP_LOADNIL,/*	A B	R(A), R(A+1), ..., R(A+B) := nil		*/
	UOP_GETUPVAL,/*	A B	R(A) := UpValue[B]				*/

	UOP_GETTABUP,/*	A B C	R(A) := UpValue[B][RK(C)]			*/
	UOP_GETTABLE,/*	A B C	R(A) := R(B)[RK(C)]				*/

	UOP_SETTABUP,/*	A B C	UpValue[A][RK(B)] := RK(C)			*/
	UOP_SETUPVAL,/*	A B	UpValue[B] := R(A)				*/
	UOP_SETTABLE,/*	A B C	R(A)[RK(B)] := RK(C)				*/

	UOP_NEWTABLE,/*	A B C	R(A) := {} (size = B,C)				*/

	UOP_SELF,/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		*/

	UOP_ADD,/*	A B C	R(A) := RK(B) + RK(C)				*/
    UOP_SUB,/*	A B C	R(A) := RK(B) - RK(C)				*/
	UOP_MUL,/*	A B C	R(A) := RK(B) * RK(C)				*/
	UOP_MOD,/*	A B C	R(A) := RK(B) % RK(C)				*/
	UOP_POW,/*	A B C	R(A) := RK(B) ^ RK(C)				*/
	UOP_DIV,/*	A B C	R(A) := RK(B) / RK(C)				*/
	UOP_IDIV,/*	A B C	R(A) := RK(B) // RK(C)				*/
	UOP_BAND,/*	A B C	R(A) := RK(B) & RK(C)				*/
	UOP_BOR,/*	A B C	R(A) := RK(B) | RK(C)				*/
	UOP_BXOR,/*	A B C	R(A) := RK(B) ~ RK(C)				*/
	UOP_SHL,/*	A B C	R(A) := RK(B) << RK(C)				*/
	UOP_SHR,/*	A B C	R(A) := RK(B) >> RK(C)				*/
	UOP_UNM,/*	A B	R(A) := -R(B)					*/
	UOP_BNOT,/*	A B	R(A) := ~R(B)					*/
	UOP_NOT,/*	A B	R(A) := not R(B)				*/
	UOP_LEN,/*	A B	R(A) := length of R(B)				*/

	UOP_CONCAT,/*	A B C	R(A) := R(B).. ... ..R(C)			*/

	UOP_JMP,/*	A sBx	pc+=sBx; if (A) close all upvalues >= R(A - 1)	*/
	UOP_EQ,/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		*/
	UOP_LT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++		*/
	UOP_LE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++		*/

	UOP_TEST,/*	A C	if not (R(A) <=> C) then pc++			*/
	UOP_TESTSET,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/

	UOP_CALL,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
	UOP_TAILCALL,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*/
    UOP_RETURN,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/

	UOP_FORLOOP,/*	A sBx	R(A)+=R(A+2);
                if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
	UOP_FORPREP,/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/

	UOP_TFORCALL,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));	*/
	UOP_TFORLOOP,/*	A sBx	if R(A+1) ~= nil then { R(A)=R(A+1); pc += sBx }*/

	UOP_SETLIST,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/

	UOP_CLOSURE,/*	A Bx	R(A) := closure(KPROTO[Bx])			*/

	UOP_VARARG,/*	A B	R(A), R(A+1), ..., R(A+B-2) = vararg		*/

	UOP_EXTRAARG,/*	Ax	extra (larger) argument for previous opcode	*/

	UOP_PUSH,/* A   top++, evalstack(top) = R(A)  */
	UOP_POP,/* A   R(A) := evalstack(top), top-- */
	UOP_GETTOP,/* A   R(A) := evalstack(top) */
	UOP_CMP, /* A B C   R(A) = 1 if RK(B) > RK(C), 0 if RK(B) == RK(C), -1 if RK(B) < RK(C) */

	UOP_CMP_EQ, /* A B C R(A) = 1 if RK(B) == RK(C), else 0 */
	UOP_CMP_NE, /* A B C R(A) = 1 if RK(B) != RK(C), else 0 */
	UOP_CMP_GT, /* A B C R(A) = 1 if RK(B) > RK(C), else 0 */
	UOP_CMP_LT, /* A B C R(A) = 1 if RK(B) < RK(C), else 0 */

	UOP_DUMMY_COUNT /* not used, just to count opcodes */
} OpCode;


#define UNUM_OPCODES	(lua_cast(int, UOP_DUMMY_COUNT))



/*===========================================================================
  Notes:
  (*) In UOP_CALL, if (B == 0) then B = top. If (C == 0), then 'top' is
  set to last_result+1, so next open instruction (UOP_CALL, UOP_RETURN,
  UOP_SETLIST) may use 'top'.

  (*) In UOP_VARARG, if (B == 0) then use actual number of varargs and
  set top (like in UOP_CALL with C == 0).

  (*) In UOP_RETURN, if (B == 0) then return up to 'top'.

  (*) In UOP_SETLIST, if (B == 0) then B = 'top'; if (C == 0) then next
  'instruction' is EXTRAARG(real C).

  (*) In UOP_LOADKX, the next 'instruction' is always EXTRAARG.

  (*) For comparisons, A specifies what condition the test should accept
  (true or false).

  (*) All 'skips' (pc++) assume that next instruction is a jump.

  ===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-1: op mode
** bits 2-3: C arg mode
** bits 4-5: B arg mode
** bit 6: instruction set register A
** bit 7: operator is a test (next instruction must be a jump)
*/

enum OpArgMask {
    OpArgN,  /* argument is not used */
    OpArgU,  /* argument is used */
    OpArgR,  /* argument is a register or a jump offset */
    OpArgK   /* argument is a constant or register/constant */
};

LUAI_DDEC const lu_byte luaP_opmodes[UNUM_OPCODES];

#define getOpMode(m)	(lua_cast(enum OpMode, luaP_opmodes[m] & 3))
#define getBMode(m)	(lua_cast(enum OpArgMask, (luaP_opmodes[m] >> 4) & 3))
#define getCMode(m)	(lua_cast(enum OpArgMask, (luaP_opmodes[m] >> 2) & 3))
#define testAMode(m)	(luaP_opmodes[m] & (1 << 6))
#define testTMode(m)	(luaP_opmodes[m] & (1 << 7))


LUAI_DDEC const char *const luaP_opnames[UNUM_OPCODES + 1];  /* opcode names */


/* number of list items to accumulate before a SETLIST instruction */
#define LFIELDS_PER_FLUSH	50


#endif
