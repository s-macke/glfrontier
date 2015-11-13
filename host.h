/*
 * This is used by the C output mode of the 68k assembler.
 * Loads 68k executable into m68k memory and applies all the relocations.
 * Executable is in an atari .prg format.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "m68000.h"

//#define likely(x)       __builtin_expect((x),1)
//#define unlikely(x)     __builtin_expect((x),0)

#define LOAD_BASE	0x0
#define MEM_SIZE	(0x110000)

union Reg {
	u16	word[2];
	u32	_u32;
	u16	_u16[2];
	u8	_u8[4];
	s32	_s32;
	s16	_s16[2];
	s8	_s8[4];
};

/* m68000 state ----------------------------------------------------- */
extern s8 m68kram[MEM_SIZE];
extern union Reg Regs[16];
/* status flags */
/* it is an optimisation that instead of having a Z (zero) flag we have
 * an nZ (not zero) flag, because this way we can usually just stick
 * the result in nZ. */
extern s32 N,nZ,V,C,X;
extern s32 bN,bnZ,bV,bC,bX;
extern s32 rdest; /* return address from interrupt. zero if none in service */
extern s32 exceptions_pending;
extern s32 exceptions_pending_nums[32];
extern u32 exception_handlers[32];

void SetReg (int reg, int val);
int GetReg (int reg);

#define GetZFlag() (!nZ)
#define GetNFlag() (N)
#define GetCFlag() (C)
#define GetVFlag() (V)
#define GetXFlag() (X)
#define SetZFlag(val) nZ = !(val)

void FlagException (int num);
void load_binfile (const char *bin_filename);

#ifdef M68K_DEBUG
#define BOUNDS_CHECK
#if 0
static inline void BOUNDS_CHECK (u32 pos, int num)
{
	if ((pos+num) > MEM_SIZE) {
		printf ("Error. 68K memory access out of bounds (address $%x, line %d).\n", pos, line_no);
		abort ();
	}
}
#endif
#endif /* M68K_DEBUG */


static inline u32 do_get_mem_long(u32 *a)
{
#ifdef __i386__
	u32 val = *a;
	__asm__ ("bswap	%0\n":"=r"(val):"0"(val));
	return val;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    return (*b << 24) | (*(b+1) << 16) | (*(b+2) << 8) | (*(b+3));
#else
    u8 *b = (u8 *)a;
    return (*b << 24) | (*(b+1) << 16) | (*(b+2) << 8) | (*(b+3));
#endif
}

static inline u16 do_get_mem_word(u16 *a)
{
#ifdef __i386__
	u16 val = *a;
	__asm__ ("rorw $8,%0" : "=q" (val) :  "0" (val));
	return val;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    return (*b << 8) | (*(b+1));
#else
    u8 *b = (u8 *)a;
    return (*b << 8) | (*(b+1));
#endif
}

static inline u8 do_get_mem_byte(u8 *a)
{
    return *a;
}

static inline void do_put_mem_long(u32 *a, u32 v)
{
#ifdef __i386__
	__asm__ ("bswap	%0\n":"=r"(v):"0"(v));
	*a = v;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    
    *b = v >> 24;
    *(b+1) = v >> 16;    
    *(b+2) = v >> 8;
    *(b+3) = v;
#else
    u8 *b = (u8 *)a;
    
    *b = v >> 24;
    *(b+1) = v >> 16;    
    *(b+2) = v >> 8;
    *(b+3) = v;
#endif
}

static inline void do_put_mem_word(u16 *a, u16 v)
{
#ifdef __i386__
	__asm__ ("rorw $8,%0" : "=q" (v) :  "0" (v));
	*a = v;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    
    *b = v >> 8;
    *(b+1) = v;
#else
    u8 *b = (u8 *)a;
    
    *b = v >> 8;
    *(b+1) = v;
#endif
}

static inline void do_put_mem_byte(u8 *a, u8 v)
{
    *a = v;
}

static inline s32 rdlong (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,4);
#endif /* M68K_DEBUG */
	return do_get_mem_long ((u32 *)(m68kram+pos));
}
static inline s16 rdword (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,2);
#endif /* M68K_DEBUG */
	return do_get_mem_word ((u16 *)(m68kram+pos));
}
static inline s8 rdbyte (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,1);
#endif /* M68K_DEBUG */
	return do_get_mem_byte ((u8 *)(m68kram+pos));
}
static inline void wrbyte (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,1);
#endif /* M68K_DEBUG */
	do_put_mem_byte ((u8 *)(m68kram+pos), (u8)val);
}
static inline void wrword (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,2);
#endif /* M68K_DEBUG */
	do_put_mem_word ((u16 *)(m68kram+pos), (u16)val);
}
static inline void wrlong (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,4);
#endif /* M68K_DEBUG */
	do_put_mem_long ((u32 *)(m68kram+pos), (u32)val);
}

#ifdef M68K_DEBUG
void m68k_print_line_no ();
#endif /* M68K_DEBUG */
