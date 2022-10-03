// "@(#)$KmKId: engine.h,v 1.6 2021-08-17 00:08:36+00 kentd Exp $"

/************************************************************************/
/*			KEGS: Apple //gs Emulator			*/
/*			Copyright 2002-2021 by Kent Dickey		*/
/*									*/
/*	This code is covered by the GNU GPL v3				*/
/*	See the file COPYING.txt or https://www.gnu.org/licenses/	*/
/*	This program is provided with no warranty			*/
/*									*/
/*	The KEGS web page is kegs.sourceforge.net			*/
/*	You may contact the author at: kadickey@alumni.princeton.edu	*/
/************************************************************************/

int
ENGINE_TYPE (Engine_reg *engine_ptr)
{
	register byte	*ptr;
	byte	*arg_ptr;
	Pc_log	*tmp_pc_ptr;
	Fplus	*fplus_ptr;
	byte	*stat;
	double	fcycles, fplus_1, fplus_2, fplus_3, fplus_x_m1, fcycles_tmp1;
	register word32	kpc, acc, xreg, yreg, direct, psr, zero, neg7, addr;
	word32	wstat, arg, stack, dbank, opcode, addr_latch, tmp1, tmp2;
	word32	getmem_tmp, save_addr, pull_tmp, tmp_bytes;

	tmp_pc_ptr = 0;
	if(tmp_pc_ptr) {		// "use" tmp_pc_ptr to avoid warning
	}

	kpc = engine_ptr->kpc;
	acc = engine_ptr->acc;
	xreg = engine_ptr->xreg;
	yreg = engine_ptr->yreg;
	stack = engine_ptr->stack;
	dbank = engine_ptr->dbank;
	direct = engine_ptr->direct;
	psr = engine_ptr->psr;
	fcycles = engine_ptr->fcycles;
	fplus_ptr = engine_ptr->fplus_ptr;
	zero = !(psr & 2);
	neg7 = psr;

	fplus_1 = fplus_ptr->plus_1;
	fplus_2 = fplus_ptr->plus_2;
	fplus_3 = fplus_ptr->plus_3;
	fplus_x_m1 = fplus_ptr->plus_x_minus_1;

	g_ret1 = 0;

	while(fcycles <= g_fcycles_end) {

		FETCH_OPCODE;

		LOG_PC_MACRO();

		switch(opcode) {
		default:
			halt_printf("acc8 unk op: %02x\n", opcode);
			arg = 9
#include "defs_instr.h"
			* 2;
			break;
#include "instable.h"
			break;
		}
		LOG_PC_MACRO2();
	}

	engine_ptr->kpc = kpc;
	engine_ptr->acc = acc;
	engine_ptr->xreg = xreg;
	engine_ptr->yreg = yreg;
	engine_ptr->stack = stack;
	engine_ptr->dbank = dbank;
	engine_ptr->direct = direct;
	engine_ptr->fcycles = fcycles;

	psr = psr & (~0x82);
	psr |= (neg7 & 0x80);
	psr |= ((!zero) << 1);

	engine_ptr->psr = psr;

	return g_ret1;
}
