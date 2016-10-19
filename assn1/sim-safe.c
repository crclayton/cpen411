/* sim-safe.c - sample functional simulator implementation */

/* SimpleScalar(TM) Tool Suite
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 * All Rights Reserved. 
 * 
 * THIS IS A LEGAL DOCUMENT, BY USING SIMPLESCALAR,
 * YOU ARE AGREEING TO THESE TERMS AND CONDITIONS.
 * 
 * No portion of this work may be used by any commercial entity, or for any
 * commercial purpose, without the prior, written permission of SimpleScalar,
 * LLC (info@simplescalar.com). Nonprofit and noncommercial use is permitted
 * as described below.
 * 
 * 1. SimpleScalar is provided AS IS, with no warranty of any kind, express
 * or implied. The user of the program accepts full responsibility for the
 * application of the program and the use of any results.
 * 
 * 2. Nonprofit and noncommercial use is encouraged. SimpleScalar may be
 * downloaded, compiled, executed, copied, and modified solely for nonprofit,
 * educational, noncommercial research, and noncommercial scholarship
 * purposes provided that this notice in its entirety accompanies all copies.
 * Copies of the modified software can be delivered to persons who use it
 * solely for nonprofit, educational, noncommercial research, and
 * noncommercial scholarship purposes provided that this notice in its
 * entirety accompanies all copies.
 l* 
 * 3. ALL COMMERCIAL USE, AND ALL USE BY FOR PROFIT ENTITIES, IS EXPRESSLY
 * PROHIBITED WITHOUT A LICENSE FROM SIMPLESCALAR, LLC (info@simplescalar.com).X
 * 
 * 4. No nonprofit user may place any restrictions on the use of this software,
 * including as modified by the user, by any other authorized user.
 * 
 * 5. Noncommercial and nonprofit users may distribute copies of SimpleScalar
 * in compiled or executable form as set forth in Section 2, provided that
 * either: (A) it is accompanied by the corresponding machine-readable source
 * code, or (B) it is accompanied by a written offer, with no time limit, to
 * give anyone a machine-readable copy of the corresponding source code in
 * return for reimbursement of the cost of distribution. This written offer
 * must permit verbatim duplication by anyone, or (C) it is distributed by
 * someone who received only the executable form, and is accompanied by a
 * copy of the written offer of source code.
 * 
 * 6. SimpleScalar was developed by Todd M. Austin, Ph.D. The tool suite is
 * currently maintained by SimpleScalar LLC (info@simplescalar.com). US Mail:
 * 2395 Timbercrest Court, Ann Arbor, MI 48105.
 * 
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include<time.h>   


#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "options.h"
#include "stats.h"
#include "sim.h"


// this is where we will store our offset counts
// offsetcounts[N] is a counter for instances of N bits needed
#define  MAXNUMS  21 
int offsetcounts[MAXNUMS];


// test if general purpose register
bool isGPR(int R){
	return R <= 31 && R >= 1;
}

// determine the offset between two values
int offset(int x, int y){
	return (int)(x-y)/8;
}

// count the number of bits needed to represent a value
int bitcount(int ofs){
        if(ofs > 0)  return (int)floor(log(ofs)/log(2.0))+2;
        if(ofs < 0)  return (int)ceil(log(-ofs)/log(2.0))+1;
	return -1;
}

// increment the counter for the offset of the two bits
void save(int x, int y){ 
        int ofs = offset(x, y);
	if(ofs == 0) return;

	int number_of_bits = bitcount(ofs);
	offsetcounts[number_of_bits]++;
}


// counts the number of bits 
int count_bits_different(int R1, int R2){
	return __builtin_popcount(R1 ^ R2);
}
/*
 * This file implements a functional simulator.  This functional simulator is
 * the simplest, most user-friendly simulator in the simplescalar tool set.
 * Unlike sim-fast, this functional simulator checks for all instruction
 * errors, and the implementation is crafted for clarity rather than speed.
 */


// counters for all the different instruction types
// for the averages bit changes, I do total_register_bit_switch/total_register_operations
static counter_t g_total_cond_branches;
static counter_t g_total_uncond_branches;
static counter_t g_total_fcomp_branches;
static counter_t g_total_fstore_branches;
static counter_t g_total_fload_branches;
static counter_t g_total_fimm_branches;
static counter_t g_total_register_bit_switch;
static counter_t g_total_register_operations;
static counter_t g_total_cycles;
static counter_t g_total_dependency_stalls;

/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/* track number of refs */
static counter_t sim_num_refs = 0;

/* maximum number of inst's to execute */
static unsigned int max_insts;

/* register simulator-specific options */
void sim_reg_options(struct opt_odb_t *odb)
{
	opt_reg_header(odb, 
			"sim-safe: This simulator implements a functional simulator.  This\n"
			"functional simulator is the simplest, most user-friendly simulator in the\n"
			"simplescalar tool set.  Unlike sim-fast, this functional simulator checks\n"
			"for all instruction errors, and the implementation is crafted for clarity\n"
			"rather than speed.\n"
		      );

	/* instruction limit */
	opt_reg_uint(odb, "-max:inst", "maximum number of inst's to execute",
			&max_insts, /* default */0,
			/* print */TRUE, /* format */NULL);

}

/* check simulator-specific option values */
	void
sim_check_options(struct opt_odb_t *odb, int argc, char **argv)
{
	/* nada */
}

/* register simulator-specific statistics */
	void
sim_reg_stats(struct stat_sdb_t *sdb)
{
	// REGISTERING ALL THE COUNTERS FOR PRINTING AT THE END
	
	stat_reg_counter(sdb, "sim_num_cond_branches" /* label for printing */,
			"total conditional branches executed" /*description*/,
			&g_total_cond_branches /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_uncond_branches" /* label for printing */,
			"total unconditional branches executed" /*description*/,
			&g_total_uncond_branches /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_fcomp_branches" /* label for printing */,
			"total floating comp branches executed" /*description*/,
			&g_total_fcomp_branches /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_fstore_branches" /* label for printing */,
			"total store branches executed" /*description*/,
			&g_total_fstore_branches /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_fload_branches" /* label for printing */,
			"total load branches executed" /*description*/,
			&g_total_fload_branches /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_fimm_branches" /* label for printing */,
			"total imm branches executed" /*description*/,
			&g_total_fimm_branches /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_total_bit_change" /* label for printing */,
			"total number of register bits change" /*description*/,
			&g_total_register_bit_switch /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);

	stat_reg_counter(sdb, "sim_num_register_change" /* label for printing */,
			"total number of operations on registers 1-32" /*description*/,
			&g_total_register_operations /* pointer to the counter */,
			0 /* initial value for the counter */, NULL);


        stat_reg_counter(sdb, "sim_num_total_cycles" /* label for printing */,
                        "total number of instruction cycles" /*description*/,
                        &g_total_cycles /* pointer to the counter */,
                        0 /* initial value for the counter */, NULL);


        stat_reg_counter(sdb, "sim_num_load_stalls" /* label for printing */,
                        "total number of stalls due to a load/branch dependency" /*description*/,
                        &g_total_dependency_stalls /* pointer to the counter */,
                        0 /* initial value for the counter */, NULL);


	stat_reg_formula(sdb, "sim_cond_branch_freq",
			"relative frequency of conditional branches",
			"sim_num_cond_branches / sim_num_insn", NULL);

	stat_reg_counter(sdb, "sim_num_insn",
			"total number of instructions executed",
			&sim_num_insn, sim_num_insn, NULL);
	stat_reg_counter(sdb, "sim_num_refs",
			"total number of loads and stores executed",
			&sim_num_refs, 0, NULL);
	stat_reg_int(sdb, "sim_elapsed_time",
			"total simulation time in seconds",
			&sim_elapsed_time, 0, NULL);
	stat_reg_formula(sdb, "sim_inst_rate",
			"simulation speed (in insts/sec)",
			"sim_num_insn / sim_elapsed_time", NULL);
	ld_reg_stats(sdb);
	mem_reg_stats(mem, sdb);
}

/* initialize the simulator */
	void
sim_init(void)
{
	sim_num_refs = 0;

	/* allocate and initialize register file */
	regs_init(&regs);


	/* allocate and initialize memory space */
	mem = mem_create("mem");
	mem_init(mem);
}

/* load program into simulated state */
	void
sim_load_prog(char *fname,		/* program to load */
		int argc, char **argv,	/* program arguments */
		char **envp)		/* program environment */
{
	/* load program text and data, set up environment, memory, and regs */
	ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);
}

/* print simulator-specific configuration information */
	void
sim_aux_config(FILE *stream)		/* output stream */
{
	/* nothing currently */
}

/* dump simulator-specific auxiliary simulator statistics */
// print the contents of the offset counts array to a file 
void sim_aux_stats(FILE *stream)		/* output stream */
{
        int i = 0;
        char text[100];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(text, sizeof(text)-1, "out%H-%M.csv", t);

        stream = fopen(text, "w+");
	
	fprintf(stream, "Bits, Offset\n");       
 
        for(i = 0; i < MAXNUMS - 1;i++){
              fprintf(stream, "%i, %i\n", i, offsetcounts[i]);
        }

	fclose(stream); 	 
	
}

/* un-initialize simulator-specific state */
	void
sim_uninit(void)
{
	/* nada */
}


/*
 * configure the execution engine
 */

/*
 * precise architected register accessors
 */

/* next program counter */
#define SET_NPC(EXPR)		(regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC			(regs.regs_PC)

/* general purpose registers */
#define GPR(N)			(regs.regs_R[N])
#define SET_GPR(N,EXPR)		(regs.regs_R[N] = (EXPR))

#if defined(TARGET_PISA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_L(N)		(regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR)	(regs.regs_F.l[(N)] = (EXPR))
#define FPR_F(N)		(regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR)	(regs.regs_F.f[(N)] = (EXPR))
#define FPR_D(N)		(regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR)	(regs.regs_F.d[(N) >> 1] = (EXPR))

/* miscellaneous register accessors */
#define SET_HI(EXPR)		(regs.regs_C.hi = (EXPR))
#define HI			(regs.regs_C.hi)
#define SET_LO(EXPR)		(regs.regs_C.lo = (EXPR))
#define LO			(regs.regs_C.lo)
#define FCC			(regs.regs_C.fcc)
#define SET_FCC(EXPR)		(regs.regs_C.fcc = (EXPR))

#elif defined(TARGET_ALPHA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_Q(N)		(regs.regs_F.q[N])
#define SET_FPR_Q(N,EXPR)	(regs.regs_F.q[N] = (EXPR))
#define FPR(N)			(regs.regs_F.d[(N)])
#define SET_FPR(N,EXPR)		(regs.regs_F.d[(N)] = (EXPR))

/* miscellaneous register accessors */
#define FPCR			(regs.regs_C.fpcr)
#define SET_FPCR(EXPR)		(regs.regs_C.fpcr = (EXPR))
#define UNIQ			(regs.regs_C.uniq)
#define SET_UNIQ(EXPR)		(regs.regs_C.uniq = (EXPR))

#else
#error No ISA target defined...
#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)						\
	((FAULT) = md_fault_none, addr = (SRC), MEM_READ_BYTE(mem, addr))
#define READ_HALF(SRC, FAULT)						\
	((FAULT) = md_fault_none, addr = (SRC), MEM_READ_HALF(mem, addr))
#define READ_WORD(SRC, FAULT)						\
	((FAULT) = md_fault_none, addr = (SRC), MEM_READ_WORD(mem, addr))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)						\
	((FAULT) = md_fault_none, addr = (SRC), MEM_READ_QWORD(mem, addr))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)					\
	((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_BYTE(mem, addr, (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)					\
	((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_HALF(mem, addr, (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)					\
	((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_WORD(mem, addr, (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)					\
	((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_QWORD(mem, addr, (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)	sys_syscall(&regs, mem_access, mem, INST, TRUE)

#define DNA         (-1)

/* general register dependence decoders */
#define DGPR(N)         (N)
#define DGPR_D(N)       ((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)       (((N)+32)&~1)
#define DFPR_F(N)       (((N)+32)&~1)
#define DFPR_D(N)       (((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI         (0+32+32)
#define DLO         (1+32+32)
#define DFCC            (2+32+32)
#define DTMP            (3+32+32)

/* start simulation, program loaded, processor precise state initialized */
void sim_main(void)
{
	md_inst_t inst;
	register md_addr_t addr;
	enum md_opcode op;
	register int is_write;
	enum md_fault_type fault;


	// variables to hold previous and new register values to compare bit changes
	int prv_reg = 0;	
	int new_reg = 0;

	int bits_diff;

	bool load_last_cycle = false;	

	// variables to store the current registers being written to
        int dst_curr1 = 0;
        int dst_curr2 = 0;

	// variables to store the register being written to last load cycle
	int dst_load1 = 0;
	int dst_load2 = 0;

	// variables to store the current source registers
        int src1 = 0; 
        int src2 = 0;
        int src3 = 0; 

	fprintf(stderr, "nclude <stdbool.h>sim: ** starting functional simulation **\n");

	/* set up initial default next PC */
	regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);



	while (TRUE)
	{

		/* maintain $r0 semantics */
		regs.regs_R[MD_REG_ZERO] = 0;
#ifdef TARGET_ALPHA
		regs.regs_F.d[MD_REG_ZERO] = 0.0;
#endif /* TARGET_ALPHA */

		/* get the next instruction to execute */
		MD_FETCH_INST(inst, mem, regs.regs_PC);

           
                /* keep an instruction count */
		sim_num_insn++;
                g_total_cycles++;

		/* set default reference address and access mode */
		addr = 0; is_write = FALSE;

		/* set default fault - none */
		fault = md_fault_none;

		/* decode the instruction */
		MD_SET_OPCODE(op, inst);
	        
	

		/* execute the instruction */
		switch (op)
		{
			// when the register is a general purpose register
			// count the bits different between the previous and new registers
			// add the difference in bits to the sum of bit switches in registers
			// then increment the total number of register operations
			// then set the previous register to be the new register for the next cycle

			// also, save the destination/source registers in our dst and src variables
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)						\
		        case OP:									\
			new_reg = O1;									\
                        dst_curr1 = GPR(O1); dst_curr2 = GPR(O2);                                       		\
                        src1 = GPR(I1); src2 = GPR(I2); src3 = GPR(I3);					\
			if( isGPR(new_reg) ) {	 							\
				bits_diff = count_bits_different(GPR(prv_reg), GPR(new_reg));		\
				g_total_register_operations++;                            		\
				g_total_register_bit_switch += bits_diff;             		    	\
                                prv_reg = new_reg;							\
			}										\
			SYMCAT(OP,_IMPL);								\
			break;
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT)    								\
			case OP:									\
													panic("attempted to execute a linking opcode");
#define CONNECT(OP)
#define DECLARE_FAULT(FAULT)						\
			{ fault = (FAULT); break; }
#include "machine.def"
			default:
				panic("attempted to execute a bogus opcode");
		}


                  
		// check each flag to increment the different operation type counters
		if (MD_OP_FLAGS(op) & F_COND) 	{
                    g_total_cond_branches++;
                }
		if (MD_OP_FLAGS(op) & F_UNCOND) {
                    g_total_uncond_branches++;  
                }
	 	if (MD_OP_FLAGS(op) & F_FCOMP /* F_FPCOND? */) 
                {   g_total_fcomp_branches++;
                }
		if (MD_OP_FLAGS(op) & F_STORE)	{
                    g_total_fstore_branches++;
                }
                if (MD_OP_FLAGS(op) & F_IMM){
                   g_total_fimm_branches++;
                }



		// if we go through a load or a branch cycle, save the destination 
		// registers and mark that this cycle was a load instruction
		if (MD_OP_FLAGS(op) & F_LOAD || MD_OP_FLAGS(op) & F_CTRL)
                {
		    dst_load1 = dst_curr1; 
		    dst_load2 = dst_curr2;
		    load_last_cycle = true;
                }
                // if we're going through a cycle that isn't a load or a branch but last 
		// we did a load or branch cycle, then we may have a dependency
                else if(load_last_cycle)
                {	
		    // if either destination register from last cycle is the 
		    // source register of this cycle, increment the counter
                    if( dst_load1 == src1 || dst_load1 == src2 || dst_load1 == src3 ||
		        dst_load2 == src1 || dst_load2 == src2 || dst_load2 == src3 ) g_total_dependency_stalls++;
                    
                    // then update the flag to indicate that last cycle wasn't a load or a branch
                    load_last_cycle = false;
                }
             
                

		// if the operation is a conditional type, save the number 
		// of bits required to change the offset
		if (MD_OP_FLAGS(op) & F_COND)   save(CPC, regs.regs_TPC); 
                

		if (fault != md_fault_none)
			fatal("fault (%d) detected @ 0x%08p", fault, regs.regs_PC);

		if (verbose)
		{
			myfprintf(stderr, "%10n [xor: 0x%08x] @ 0x%08p: ",
					sim_num_insn, md_xor_regs(&regs), regs.regs_PC);
			md_print_insn(inst, regs.regs_PC, stderr);
			if (MD_OP_FLAGS(op) & F_MEM)
				myfprintf(stderr, "  mem: 0x%08p", addr);
			fprintf(stderr, "\n");
			/* fflush(stderr); */
		}

		if (MD_OP_FLAGS(op) & F_MEM)
		{
			sim_num_refs++;
			if (MD_OP_FLAGS(op) & F_STORE)
				is_write = TRUE;
		}

		/* go to the next instruction */
		regs.regs_PC = regs.regs_NPC;
		regs.regs_NPC += sizeof(md_inst_t);

		//printf("%i", sizeof(md_inst_t));
		//printf("%i", regs.regs_PC - regs.regs_TPC);

		/* finish early? */
		if (max_insts && sim_num_insn >= max_insts){
			// printf("AVERAGE CHANGE: %f", (float)bytes_diff_counter/(float)reg_change_counter);
			return;
		} 
	}
}


