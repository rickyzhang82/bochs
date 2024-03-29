/////////////////////////////////////////////////////////////////////////
// $Id: memory.cc,v 1.27 2003/03/02 23:59:12 cbothamy Exp $
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







#include "bochs.h"
#define LOG_THIS BX_MEM_THIS

#if BX_PROVIDE_CPU_MEMORY

  void BX_CPP_AttrRegparmN(3)
BX_MEM_C::writePhysicalPage(BX_CPU_C *cpu, Bit32u addr, unsigned len, void *data)
{
  Bit8u *data_ptr;
  Bit32u a20addr;

  // Note: accesses should always be contained within a single page now.

#if BX_IODEBUG_SUPPORT
  bx_iodebug_c::mem_write(cpu, addr, len, data);
#endif

  a20addr = A20ADDR(addr);
  BX_INSTR_PHY_WRITE(cpu->which_cpu(), a20addr, len);

#if BX_DEBUGGER
  // (mch) Check for physical write break points, TODO
  // (bbd) Each breakpoint should have an associated CPU#, TODO
  for (int i = 0; i < num_write_watchpoints; i++)
        if (write_watchpoint[i] == a20addr) {
	      BX_CPU(0)->watchpoint = a20addr;
              BX_CPU(0)->break_point = BREAK_POINT_WRITE;
              break;
        }
#endif

#if BX_SupportICache
  if (a20addr < BX_MEM_THIS len)
    cpu->iCache.decWriteStamp(cpu, a20addr);
#endif

  if ( a20addr <= BX_MEM_THIS len ) {
    // all of data is within limits of physical memory
    if ( (a20addr & 0xfff80000) != 0x00080000 ) {
      if (len == 4) {
        WriteHostDWordToLittleEndian(&vector[a20addr], *(Bit32u*)data);
        BX_DBG_DIRTY_PAGE(a20addr >> 12);
        return;
        }
      if (len == 2) {
        WriteHostWordToLittleEndian(&vector[a20addr], *(Bit16u*)data);
        BX_DBG_DIRTY_PAGE(a20addr >> 12);
        return;
        }
      if (len == 1) {
        * ((Bit8u *) (&vector[a20addr])) = * (Bit8u *) data;
        BX_DBG_DIRTY_PAGE(a20addr >> 12);
        return;
        }
      // len == other, just fall thru to special cases handling
      }

#ifdef BX_LITTLE_ENDIAN
  data_ptr = (Bit8u *) data;
#else // BX_BIG_ENDIAN
  data_ptr = (Bit8u *) data + (len - 1);
#endif

write_one:
    if ( (a20addr & 0xfff80000) != 0x00080000 ) {
      // addr *not* in range 00080000 .. 000FFFFF
      vector[a20addr] = *data_ptr;
      BX_DBG_DIRTY_PAGE(a20addr >> 12);
inc_one:
      if (len == 1) return;
      len--;
      a20addr++;
#ifdef BX_LITTLE_ENDIAN
      data_ptr++;
#else // BX_BIG_ENDIAN
      data_ptr--;
#endif
      goto write_one;
      }

    // addr in range 00080000 .. 000FFFFF

    if (a20addr <= 0x0009ffff) {
      // regular memory 80000 .. 9FFFF
      vector[a20addr] = *data_ptr;
      BX_DBG_DIRTY_PAGE(a20addr >> 12);
      goto inc_one;
      }
    if (a20addr <= 0x000bffff) {
      // VGA memory A0000 .. BFFFF
      DEV_vga_mem_write(a20addr, *data_ptr);
      BX_DBG_DIRTY_PAGE(a20addr >> 12);
      BX_DBG_UCMEM_REPORT(a20addr, 1, BX_WRITE, *data_ptr); // obsolete
      goto inc_one;
      }
    // adapter ROM     C0000 .. DFFFF
    // ROM BIOS memory E0000 .. FFFFF
    // (ignore write)
    //BX_INFO(("ROM lock %08x: len=%u",
    //  (unsigned) a20addr, (unsigned) len));
#if BX_PCI_SUPPORT == 0
#if BX_SHADOW_RAM
    // Write it since its in shadow RAM
    vector[a20addr] = *data_ptr;
    BX_DBG_DIRTY_PAGE(a20addr >> 12);
#else
    // ignore write to ROM
#endif
#else
    // Write Based on 440fx Programming
    if (bx_options.Oi440FXSupport->get () &&
        ((a20addr >= 0xC0000) && (a20addr <= 0xFFFFF))) {
      switch (DEV_pci_wr_memtype(a20addr & 0xFC000)) {
        case 0x1:   // Writes to ShadowRAM
//        BX_INFO(("Writing to ShadowRAM %08x, len %u ! ", (unsigned) a20addr, (unsigned) len));
          shadow[a20addr - 0xc0000] = *data_ptr;
          BX_DBG_DIRTY_PAGE(a20addr >> 12);
          goto inc_one;

        case 0x0:   // Writes to ROM, Inhibit
          BX_DEBUG(("Write to ROM ignored: address %08x, data %02x", (unsigned) a20addr, *data_ptr));
          goto inc_one;
        default:
          BX_PANIC(("writePhysicalPage: default case"));
          goto inc_one;
        }
      }
#endif
    goto inc_one;
    }

  else {
    // some or all of data is outside limits of physical memory
    unsigned i;

#ifdef BX_LITTLE_ENDIAN
  data_ptr = (Bit8u *) data;
#else // BX_BIG_ENDIAN
  data_ptr = (Bit8u *) data + (len - 1);
#endif


#if BX_SUPPORT_VBE
    // Check VBE LFB support
    
    if ((a20addr >= VBE_DISPI_LFB_PHYSICAL_ADDRESS) &&
        (a20addr <  (VBE_DISPI_LFB_PHYSICAL_ADDRESS +  VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES)))
    {
      for (i = 0; i < len; i++) {
        
        //if (a20addr < BX_MEM_THIS len) {
          //vector[a20addr] = *data_ptr;
          //BX_DBG_DIRTY_PAGE(a20addr >> 12);
          DEV_vga_mem_write(a20addr, *data_ptr);
        //  }
        
        // otherwise ignore byte, since it overruns memory
        addr++;
        a20addr = (addr);
#ifdef BX_LITTLE_ENDIAN
        data_ptr++;
#else // BX_BIG_ENDIAN
        data_ptr--;
#endif
      }
      return;
    }
    
#endif    
 

#if BX_SUPPORT_APIC
    bx_generic_apic_c *local_apic = &cpu->local_apic;
    bx_generic_apic_c *ioapic = bx_devices.ioapic;
    if (local_apic->is_selected (a20addr, len)) {
      local_apic->write (a20addr, (Bit32u *)data, len);
      return;
    } else if (ioapic->is_selected (a20addr, len)) {
      ioapic->write (a20addr, (Bit32u *)data, len);
      return;
    }
    else 
#endif
    for (i = 0; i < len; i++) {
      if (a20addr < BX_MEM_THIS len) {
        vector[a20addr] = *data_ptr;
        BX_DBG_DIRTY_PAGE(a20addr >> 12);
        }
      // otherwise ignore byte, since it overruns memory
      addr++;
      a20addr = (addr);
#ifdef BX_LITTLE_ENDIAN
      data_ptr++;
#else // BX_BIG_ENDIAN
      data_ptr--;
#endif
      }
    return;
    }
}


  void BX_CPP_AttrRegparmN(3)
BX_MEM_C::readPhysicalPage(BX_CPU_C *cpu, Bit32u addr, unsigned len, void *data)
{
  Bit8u *data_ptr;
  Bit32u a20addr;

#if BX_IODEBUG_SUPPORT
  bx_iodebug_c::mem_read(cpu, addr, len, data);
#endif
  

  a20addr = A20ADDR(addr);
  BX_INSTR_PHY_READ(cpu->which_cpu(), a20addr, len);

#if BX_DEBUGGER
  // (mch) Check for physical read break points, TODO
  // (bbd) Each breakpoint should have an associated CPU#, TODO
  for (int i = 0; i < num_read_watchpoints; i++)
        if (read_watchpoint[i] == a20addr) {
	      BX_CPU(0)->watchpoint = a20addr;
              BX_CPU(0)->break_point = BREAK_POINT_READ;
              break;
        }
#endif

  if ( (a20addr + len) <= BX_MEM_THIS len ) {
    // all of data is within limits of physical memory
    if ( (a20addr & 0xfff80000) != 0x00080000 ) {
      if (len == 4) {
        ReadHostDWordFromLittleEndian(&vector[a20addr], * (Bit32u*) data);
        return;
        }
      if (len == 2) {
        ReadHostWordFromLittleEndian(&vector[a20addr], * (Bit16u*) data);
        return;
        }
      if (len == 1) {
        * (Bit8u *) data =  * ((Bit8u *) (&vector[a20addr]));
        return;
        }
      // len == 3 case can just fall thru to special cases handling
      }


#ifdef BX_LITTLE_ENDIAN
    data_ptr = (Bit8u *) data;
#else // BX_BIG_ENDIAN
    data_ptr = (Bit8u *) data + (len - 1);
#endif



read_one:
    if ( (a20addr & 0xfff80000) != 0x00080000 ) {
      // addr *not* in range 00080000 .. 000FFFFF
      *data_ptr = vector[a20addr];
inc_one:
      if (len == 1) return;
      len--;
      a20addr++;
#ifdef BX_LITTLE_ENDIAN
      data_ptr++;
#else // BX_BIG_ENDIAN
      data_ptr--;
#endif
      goto read_one;
      }

    // addr in range 00080000 .. 000FFFFF
#if BX_PCI_SUPPORT == 0
    if ((a20addr <= 0x0009ffff) || (a20addr >= 0x000c0000) ) {
      // regular memory 80000 .. 9FFFF, C0000 .. F0000
      *data_ptr = vector[a20addr];
      goto inc_one;
      }
    // VGA memory A0000 .. BFFFF
    *data_ptr = DEV_vga_mem_read(a20addr);
    BX_DBG_UCMEM_REPORT(a20addr, 1, BX_READ, *data_ptr); // obsolete
    goto inc_one;
#else   // #if BX_PCI_SUPPORT == 0
    if (a20addr <= 0x0009ffff) {
      *data_ptr = vector[a20addr];
      goto inc_one;
      }
    if (a20addr <= 0x000BFFFF) {
      // VGA memory A0000 .. BFFFF
      *data_ptr = DEV_vga_mem_read(a20addr);
      BX_DBG_UCMEM_REPORT(a20addr, 1, BX_READ, *data_ptr);
      goto inc_one;
      }

    // a20addr in C0000 .. FFFFF
    if (!bx_options.Oi440FXSupport->get ()) {
      *data_ptr = vector[a20addr];
      goto inc_one;
      }
    else {
      switch (DEV_pci_rd_memtype(a20addr & 0xFC000)) {
        case 0x1:   // Read from ShadowRAM
          *data_ptr = shadow[a20addr - 0xc0000];
          BX_INFO(("Reading from ShadowRAM %08x, Data %02x ", (unsigned) a20addr, *data_ptr));
          goto inc_one;

        case 0x0:   // Read from ROM
          *data_ptr = vector[a20addr];
          //BX_INFO(("Reading from ROM %08x, Data %02x  ", (unsigned) a20addr, *data_ptr));
          goto inc_one;
        default:
          BX_PANIC(("::readPhysicalPage: default case"));
        }
      }
    goto inc_one;
#endif  // #if BX_PCI_SUPPORT == 0
    }
  else {
    // some or all of data is outside limits of physical memory
    unsigned i;

#ifdef BX_LITTLE_ENDIAN
    data_ptr = (Bit8u *) data;
#else // BX_BIG_ENDIAN
    data_ptr = (Bit8u *) data + (len - 1);
#endif

#if BX_SUPPORT_VBE
    // Check VBE LFB support
    
    if ((a20addr >= VBE_DISPI_LFB_PHYSICAL_ADDRESS) &&
        (a20addr <  (VBE_DISPI_LFB_PHYSICAL_ADDRESS +  VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES)))
    {
      for (i = 0; i < len; i++) {
        
        //if (a20addr < BX_MEM_THIS len) {
          //vector[a20addr] = *data_ptr;
          //BX_DBG_DIRTY_PAGE(a20addr >> 12);
          *data_ptr = DEV_vga_mem_read(a20addr);
        //  }
        
        // otherwise ignore byte, since it overruns memory
        addr++;
        a20addr = (addr);
#ifdef BX_LITTLE_ENDIAN
        data_ptr++;
#else // BX_BIG_ENDIAN
        data_ptr--;
#endif
      }
      return;
    }
    
#endif    


#if BX_SUPPORT_APIC
    bx_generic_apic_c *local_apic = &cpu->local_apic;
    bx_generic_apic_c *ioapic = bx_devices.ioapic;
    if (local_apic->is_selected (addr, len)) {
      local_apic->read (addr, data, len);
      return;
    } else if (ioapic->is_selected (addr, len)) {
      ioapic->read (addr, data, len);
      return;
    }
#endif
    for (i = 0; i < len; i++) {
#if BX_PCI_SUPPORT == 0
      if (a20addr < BX_MEM_THIS len)
        *data_ptr = vector[a20addr];
      else
        *data_ptr = 0xff;
#else   // BX_PCI_SUPPORT == 0
      if (a20addr < BX_MEM_THIS len) {
        if ((a20addr >= 0x000C0000) && (a20addr <= 0x000FFFFF)) {
          if (!bx_options.Oi440FXSupport->get ())
            *data_ptr = vector[a20addr];
          else {
            switch (DEV_pci_rd_memtype(a20addr & 0xFC000)) {
              case 0x0:   // Read from ROM
                *data_ptr = vector[a20addr];
                //BX_INFO(("Reading from ROM %08x, Data %02x ", (unsigned) a20addr, *data_ptr));
                break;

              case 0x1:   // Read from Shadow RAM
                *data_ptr = shadow[a20addr - 0xc0000];
                BX_INFO(("Reading from ShadowRAM %08x, Data %02x  ", (unsigned) a20addr, *data_ptr));
                break;
              default:
                BX_PANIC(("readPhysicalPage: default case"));
              } // Switch
            }
          }
        else {
          *data_ptr = vector[a20addr];
          BX_INFO(("Reading from Norm %08x, Data %02x  ", (unsigned) a20addr, *data_ptr));
          }
        }
      else 
        *data_ptr = 0xff;
#endif  // BX_PCI_SUPPORT == 0
      addr++;
      a20addr = (addr);
#ifdef BX_LITTLE_ENDIAN
      data_ptr++;
#else // BX_BIG_ENDIAN
      data_ptr--;
#endif
      }
    return;
    }
}

#endif // #if BX_PROVIDE_CPU_MEMORY
