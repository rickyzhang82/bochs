/////////////////////////////////////////////////////////////////////////
// $Id: mult32.cc,v 1.11 2002/10/25 11:44:35 bdenney Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001  MandrakeSoft S.A.
//
//    MandrakeSoft S.A.
//    43, rue d'Aboukir
//    75002 Paris - France
//    http://www.linux-mandrake.com/
//    http://www.mandrakesoft.com/
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA






#define NEED_CPU_REG_SHORTCUTS 1
#include "bochs.h"
#define LOG_THIS BX_CPU_THIS_PTR



#if BX_SUPPORT_X86_64==0
#define RAX EAX
#define RDX EDX
#endif


  void
BX_CPU_C::MUL_EAXEd(bxInstruction_c *i)
{
    Bit32u op1_32, op2_32, product_32h, product_32l;
    Bit64u product_64;
    bx_bool temp_flag;

    op1_32 = EAX;

    /* op2 is a register or memory reference */
    if (i->modC0()) {
      op2_32 = BX_READ_32BIT_REG(i->rm());
      }
    else {
      /* pointer, segment address pair */
      read_virtual_dword(i->seg(), RMAddr(i), &op2_32);
      }

    product_64 = ((Bit64u) op1_32) * ((Bit64u) op2_32);

    product_32l = (Bit32u) (product_64 & 0xFFFFFFFF);
    product_32h = (Bit32u) (product_64 >> 32);

    /* now write product back to destination */

    RAX = product_32l;
    RDX = product_32h;

    /* set eflags:
     * MUL affects the following flags: C,O
     */

    temp_flag = (product_32h != 0);
    SET_FLAGS_OxxxxC(temp_flag, temp_flag);
}



  void
BX_CPU_C::IMUL_EAXEd(bxInstruction_c *i)
{
    Bit32s op1_32, op2_32;
    Bit64s product_64;
    Bit32u product_32h, product_32l;

    op1_32 = EAX;

    /* op2 is a register or memory reference */
    if (i->modC0()) {
      op2_32 = BX_READ_32BIT_REG(i->rm());
      }
    else {
      /* pointer, segment address pair */
      read_virtual_dword(i->seg(), RMAddr(i), (Bit32u *) &op2_32);
      }

    product_64 = ((Bit64s) op1_32) * ((Bit64s) op2_32);

    product_32l = (Bit32u) (product_64 & 0xFFFFFFFF);
    product_32h = (Bit32u) (product_64 >> 32);

    /* now write product back to destination */

    RAX = product_32l;
    RDX = product_32h;

    /* set eflags:
     * IMUL affects the following flags: C,O
     * IMUL r/m16: condition for clearing CF & OF:
     *   EDX:EAX = sign-extend of EAX
     */

    if ( (EDX==0xffffffff) && (EAX & 0x80000000) ) {
      SET_FLAGS_OxxxxC(0, 0);
      }
    else if ( (EDX==0x00000000) && (EAX < 0x80000000) ) {
      SET_FLAGS_OxxxxC(0, 0);
      }
    else {
      SET_FLAGS_OxxxxC(1, 1);
      }
}


  void
BX_CPU_C::DIV_EAXEd(bxInstruction_c *i)
{
    Bit32u op2_32, remainder_32, quotient_32l;
    Bit64u op1_64, quotient_64;

    op1_64 = (((Bit64u) EDX) << 32) + ((Bit64u) EAX);

    /* op2 is a register or memory reference */
    if (i->modC0()) {
      op2_32 = BX_READ_32BIT_REG(i->rm());
      }
    else {
      /* pointer, segment address pair */
      read_virtual_dword(i->seg(), RMAddr(i), &op2_32);
      }

    if (op2_32 == 0) {
      exception(BX_DE_EXCEPTION, 0, 0);
      }
    quotient_64 = op1_64 / op2_32;
    remainder_32 = (Bit32u) (op1_64 % op2_32);
    quotient_32l = (Bit32u) (quotient_64 & 0xFFFFFFFF);

    if (quotient_64 != quotient_32l) {
      exception(BX_DE_EXCEPTION, 0, 0);
      }

    /* set EFLAGS:
     * DIV affects the following flags: O,S,Z,A,P,C are undefined
     */

    /* now write quotient back to destination */

    RAX = quotient_32l;
    RDX = remainder_32;
}


  void
BX_CPU_C::IDIV_EAXEd(bxInstruction_c *i)
{
    Bit32s op2_32, remainder_32, quotient_32l;
    Bit64s op1_64, quotient_64;

    op1_64 = (((Bit64u) EDX) << 32) | ((Bit64u) EAX);

    /* op2 is a register or memory reference */
    if (i->modC0()) {
      op2_32 = BX_READ_32BIT_REG(i->rm());
      }
    else {
      /* pointer, segment address pair */
      read_virtual_dword(i->seg(), RMAddr(i), (Bit32u *) &op2_32);
      }

    if (op2_32 == 0) {
      exception(BX_DE_EXCEPTION, 0, 0);
      }
    quotient_64 = op1_64 / op2_32;
    remainder_32 = (Bit32s) (op1_64 % op2_32);
    quotient_32l = (Bit32s) (quotient_64 & 0xFFFFFFFF);

    if (quotient_64 != quotient_32l) {
      exception(BX_DE_EXCEPTION, 0, 0);
      }

    /* set EFLAGS:
     * IDIV affects the following flags: O,S,Z,A,P,C are undefined
     */

    /* now write quotient back to destination */

    RAX = quotient_32l;
    RDX = remainder_32;
}


  void
BX_CPU_C::IMUL_GdEdId(bxInstruction_c *i)
{
#if BX_CPU_LEVEL < 2
  BX_PANIC(("IMUL_GdEdId() unsupported on 8086!"));
#else


    Bit32s op2_32, op3_32, product_32;
    Bit64s product_64;

    op3_32 = i->Id();

    /* op2 is a register or memory reference */
    if (i->modC0()) {
      op2_32 = BX_READ_32BIT_REG(i->rm());
      }
    else {
      /* pointer, segment address pair */
      read_virtual_dword(i->seg(), RMAddr(i), (Bit32u *) &op2_32);
      }

    product_32 = op2_32 * op3_32;
    product_64 = ((Bit64s) op2_32) * ((Bit64s) op3_32);

    /* now write product back to destination */
    BX_WRITE_32BIT_REGZ(i->nnn(), product_32);

    /* set eflags:
     * IMUL affects the following flags: C,O
     * IMUL r16,r/m16,imm16: condition for clearing CF & OF:
     *   result exactly fits within r16
     */

    if (product_64 == product_32) {
      SET_FLAGS_OxxxxC(0, 0);
      }
    else {
      SET_FLAGS_OxxxxC(1, 1);
      }
#endif
}


  void
BX_CPU_C::IMUL_GdEd(bxInstruction_c *i)
{
#if BX_CPU_LEVEL < 3
  BX_PANIC(("IMUL_GvEv() unsupported on 8086!"));
#else

    Bit32s op1_32, op2_32, product_32;
    Bit64s product_64;

    /* op2 is a register or memory reference */
    if (i->modC0()) {
      op2_32 = BX_READ_32BIT_REG(i->rm());
      }
    else {
      /* pointer, segment address pair */
      read_virtual_dword(i->seg(), RMAddr(i), (Bit32u *) &op2_32);
      }

    op1_32 = BX_READ_32BIT_REG(i->nnn());

    product_32 = op1_32 * op2_32;
    product_64 = ((Bit64s) op1_32) * ((Bit64s) op2_32);

    /* now write product back to destination */
    BX_WRITE_32BIT_REGZ(i->nnn(), product_32);

    /* set eflags:
     * IMUL affects the following flags: C,O
     * IMUL r16,r/m16,imm16: condition for clearing CF & OF:
     *   result exactly fits within r16
     */

    if (product_64 == product_32) {
      SET_FLAGS_OxxxxC(0, 0);
      }
    else {
      SET_FLAGS_OxxxxC(1, 1);
      }
#endif
}
