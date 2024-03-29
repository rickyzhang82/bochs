/*---------------------------------------------------------------------------+
 |  fpu_aux.c                                                                |
 |  $Id: fpu_aux.c,v 1.7 2003/11/01 18:36:19 sshwarts Exp $
 |                                                                           |
 | Code to implement some of the FPU auxiliary instructions.                 |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997                                         |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"
#include "status_w.h"
#include "control_w.h"

static void fnop(void) { }

void fclex(void)
{
  FPU_partial_status &= ~(SW_Backward|SW_Summary|SW_Stack_Fault|SW_Precision|
		   SW_Underflow|SW_Overflow|SW_Zero_Div|SW_Denorm_Op|
		   SW_Invalid);
  no_ip_update = 1;
}

/* Needs to be externally visible */
void finit()
{
  FPU_control_word = 0x037f;
  FPU_partial_status = 0;
  FPU_tos = 0;            /* We don't keep top in the status word internally. */
  FPU_tag_word = 0xffff;
  /* The behaviour is different from that detailed in
     Section 15.1.6 of the Intel manual */
  FPU_operand_address.offset = 0;
  FPU_operand_address.selector = 0;
  FPU_instruction_address.offset = 0;
  FPU_instruction_address.selector = 0;
  FPU_instruction_address.opcode = 0;
  no_ip_update = 1;
}

/*
 * These are nops on the i387..
 */
#define feni fnop
#define fdisi fnop
#define fsetpm fnop

static FUNC const finit_table[] = {
  feni, fdisi, fclex, finit,
  fsetpm, FPU_illegal, FPU_illegal, FPU_illegal
};

void finit_()
{
  (finit_table[FPU_rm])();
}

static void fstsw_ax(void)
{
  fpu_set_ax(status_word());
  no_ip_update = 1;
}

static FUNC const fstsw_table[] = {
  fstsw_ax, FPU_illegal, FPU_illegal, FPU_illegal,
  FPU_illegal, FPU_illegal, FPU_illegal, FPU_illegal
};

void fstsw_()
{
  (fstsw_table[FPU_rm])();
}


static FUNC const fp_nop_table[] = {
  fnop, FPU_illegal, FPU_illegal, FPU_illegal,
  FPU_illegal, FPU_illegal, FPU_illegal, FPU_illegal
};

void fp_nop()
{
  (fp_nop_table[FPU_rm])();
}

void fld_i_()
{
  FPU_REG *st_new_ptr;
  int i;
  u_char tag;

  if (FPU_stackoverflow(&st_new_ptr))
    { FPU_stack_overflow(); 
      return; 
    }

  /* fld st(i) */
  i = FPU_rm;
  if ( NOT_EMPTY(i) )
    {
      reg_copy(&st(i), st_new_ptr);
      tag = FPU_gettagi(i);
      FPU_push();
      FPU_settag0(tag);
    }
  else
    {
      if ( FPU_control_word & CW_Invalid )
	{
	  /* The masked response */
	  FPU_stack_underflow();
	}
      else
	EXCEPTION(EX_StackUnder);
    }

}

void fxch_i()
{
  /* fxch st(i) */
  FPU_REG t;
  int i = FPU_rm;
  FPU_REG *st0_ptr = &st(0), *sti_ptr = &st(i);
  s32 tag_word = FPU_tag_word;
  int regnr = FPU_tos & 7, regnri = ((regnr + i) & 7);
  u_char st0_tag = (tag_word >> (regnr*2)) & 3;
  u_char sti_tag = (tag_word >> (regnri*2)) & 3;

  clear_C1();

  if ( st0_tag == TAG_Empty )
    {
      if ( sti_tag == TAG_Empty )
	{
	  FPU_stack_underflow();
	  FPU_stack_underflow_i(i);
	  return;
	}
      if ( FPU_control_word & CW_Invalid )
	{
	  /* Masked response */
	  FPU_copy_to_reg0(sti_ptr, sti_tag);
	}
      FPU_stack_underflow_i(i);
      return;
    }
  if ( sti_tag == TAG_Empty )
    {
      if ( FPU_control_word & CW_Invalid )
	{
	  /* Masked response */
	  FPU_copy_to_regi(st0_ptr, st0_tag, i);
	}
      FPU_stack_underflow();
      return;
    }

  reg_copy(st0_ptr, &t);
  reg_copy(sti_ptr, st0_ptr);
  reg_copy(&t, sti_ptr);

  tag_word &= ~(3 << (regnr*2)) & ~(3 << (regnri*2));
  tag_word |= (sti_tag << (regnr*2)) | (st0_tag << (regnri*2));
  FPU_tag_word = tag_word;
}

void ffree_()
{
  /* ffree st(i) */
  FPU_settagi(FPU_rm, TAG_Empty);
}

void fst_i_()
{
  /* fst st(i) */
  FPU_copy_to_regi(&st(0), FPU_gettag0(), FPU_rm);
}

void fstp_i()
{
  /* fstp st(i) */
  FPU_copy_to_regi(&st(0), FPU_gettag0(), FPU_rm);
  FPU_pop();
}
