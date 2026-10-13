/* MCF5206E core timing, MCF5206EUM tables 3-5 through 3-10.
 * These are core cycles with cache hits and zero-wait-state operand accesses.
 * Return zero for instructions outside the timing model; the caller retains
 * the inherited 68020 estimate for those instructions and for exceptions.
 * MOVEM's per-register cost is charged by its existing handler. Conditional
 * branches use a three-cycle base and adjust for direction and outcome.
 */
#ifndef M68K_CF_CYCLES_H
#define M68K_CF_CYCLES_H

static unsigned char m68ki_cf_base_cycles(unsigned int op)
{
	const unsigned int family = op >> 12;
	const unsigned int mode = (op >> 3) & 7;
	const unsigned int reg = op & 7;
	const unsigned int size = (op >> 6) & 3;
	const int indexed = mode == 6 || (mode == 7 && reg == 3);
	const int immediate = mode == 7 && reg == 4;
	const int source = mode < 7 || reg <= 4;
	const int memory = mode >= 2 && (mode < 7 || reg <= 1);
	const int control = mode == 2 || mode == 5 || mode == 6
		|| (mode == 7 && reg <= 3);

	/* MOVE source/destination matrices, tables 3-5 and 3-6. */
	if(family >= 1 && family <= 3)
	{
		const unsigned int dm = (op >> 6) & 7;
		const unsigned int dr = (op >> 9) & 7;
		const int source_reg = mode <= 1;
		const int simple_memory = mode >= 2 && mode <= 4;
		const int load_cycles = family == 2 ? 2 : 3;
		if(!source || (family == 1 && (mode == 1 || dm == 1))
			|| (dm == 7 && dr > 1))
			return 0;
		if(dm == 5 && !(source_reg || simple_memory || mode == 5
			|| (mode == 7 && reg == 2)))
			return 0;
		if(dm >= 6 && !(source_reg || simple_memory))
			return 0;
		if(dm == 6)
			return source_reg ? 2 : load_cycles + 1;
		if(indexed)
			return load_cycles + 1;
		if(source_reg || (dm <= 1 && immediate))
			return 1;
		return load_cycles;
	}

	if((op & 0xfff0) == 0x4e40) return 15; /* TRAP */
	if((op & 0xf100) == 0x7000) return 1; /* MOVEQ */
	if(family == 6 && (op & 0xff) != 0xff)
		return (op & 0x0f00) == 0 ? 2 : 3; /* BRA, BSR, Bcc */

	/* Register and immediate long arithmetic/logical operations. */
	switch(op & 0xfff8)
	{
	case 0x0080: case 0x0280: case 0x0480: case 0x0680:
	case 0x0a80: case 0x0c80: /* ORI, ANDI, SUBI, ADDI, EORI, CMPI */
	case 0x4080: case 0x4480: case 0x4680: /* NEGX, NEG, NOT */
	case 0x4840: case 0x4880: case 0x48c0: case 0x49c0: /* SWAP, EXT */
	case 0x40c0: case 0x42c0: case 0x44c0: /* SR/CCR register moves */
		return 1;
	case 0x46c0: return 7; /* MOVE Dn,SR */
	case 0x4e50: case 0x4e58: return 2; /* LINK.W, UNLK */
	}
	if((op & 0xf0f8) == 0x50c0) return 1; /* Scc Dn */
	if((op & 0xf0c0) == 0x5080 && (mode <= 1 || memory))
		return mode <= 1 ? 1 : indexed ? 4 : 3; /* ADDQ/SUBQ.L */
	if(family == 0xe && size == 2 && (op & 0x18) <= 8)
		return 1; /* ASL/ASR/LSL/LSR.L, register or immediate count */
	if((op & 0xf130) == 0xd100 || (op & 0xf130) == 0x9100)
		if(size == 2 && (op & 8) == 0) return 1; /* ADDX/SUBX.L */

	if(family == 8 || family == 9 || family == 0xb || family == 0xc || family == 0xd)
	{
		const unsigned int operation = (op >> 6) & 7;
		if((operation == 2 || ((family == 9 || family == 0xb || family == 0xd)
			&& operation == 7)) && source)
			return mode <= 1 || immediate ? 1 : indexed ? 4 : 3;
		if(operation == 6 && (memory || (family == 0xb && mode == 0)))
			return mode == 0 ? 1 : indexed ? 4 : 3;
		if((family == 8 || family == 0xc) && (operation == 3 || operation == 7)
			&& source && mode != 1)
		{
			const int base = family == 8 ? 20 : 9; /* DIV.W, MUL.W */
			return mode == 0 || immediate ? base : base + (family == 8 ? 3 : 2) + indexed;
		}
	}

	/* Bit operations, table 3-8. */
	if((op & 0xf100) == 0x0100 || (op & 0xff00) == 0x0800)
	{
		const int static_bit = (op & 0x0100) == 0;
		const int test = (op & 0xc0) == 0;
		if(mode == 0) return static_bit && test ? 1 : 2;
		if(memory && (!static_bit || mode <= 5))
			return (test ? 3 : 4) + indexed;
		if(static_bit && test && immediate) return 1;
	}

	if((op & 0xff00) == 0x4200 && size != 3 && (mode == 0 || memory))
		return indexed ? 2 : 1; /* CLR */
	if((op & 0xff00) == 0x4a00 && size != 3 && source && !(size == 0 && mode == 1))
		return mode <= 1 || immediate ? 1 : (size == 2 ? 2 : 3) + indexed; /* TST */
	if((op & 0xf1c0) == 0x41c0 && control) return indexed ? 2 : 1; /* LEA */
	if((op & 0xffc0) == 0x4840 && control) return indexed ? 3 : 2; /* PEA */
	if(((op & 0xffc0) == 0x4e80 || (op & 0xffc0) == 0x4ec0) && control)
		return indexed ? 4 : 3; /* JSR/JMP */
	if((op & 0xfb80) == 0x4880 && (op & 0x40) && (mode == 2 || mode == 5))
		return 1; /* MOVEM.L: handler adds n */
	if((op & 0xffc0) == 0x4c00 && (mode == 0 || (mode >= 2 && mode <= 5)))
		return mode == 0 ? 18 : 20; /* MUL.L upper bound; early termination not modeled */
	if((op & 0xffc0) == 0x4c40 && (mode == 0 || (mode >= 2 && mode <= 5)))
		return 35; /* DIV.L/REM.L */

	switch(op)
	{
	case 0x44fc: return 1; /* immediate CCR */
	case 0x46fc: return 7; /* immediate SR; supervisor case adjusted in handler */
	case 0x4e71: case 0x4e72: return 3; /* NOP, STOP */
	case 0x4e73: return 10; /* RTE */
	case 0x4e75: return 5; /* RTS */
	case 0x4e7b: return 9; /* MOVEC */
	case 0x51fa: case 0x51fb: case 0x51fc: return 1; /* TRAPF */
	}
	return 0;
}
#endif
