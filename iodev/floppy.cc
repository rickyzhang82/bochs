/////////////////////////////////////////////////////////////////////////
// $Id: floppy.cc,v 1.69 2003/12/18 20:04:49 vruppert Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2002  MandrakeSoft S.A.
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
//
//
// Floppy Disk Controller Docs:
// Intel 82077A Data sheet
//   ftp://void-core.2y.net/pub/docs/fdc/82077AA_FloppyControllerDatasheet.pdf
// Intel 82078 Data sheet
//   ftp://download.intel.com/design/periphrl/datashts/29047403.PDF
// Other FDC references
//   http://debs.future.easyspace.com/Programming/Hardware/FDC/floppy.html
// And a port list:
//   http://mudlist.eorbit.net/~adam/pickey/ports.html
//

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE 
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE


extern "C" {
#include <errno.h>
}

#ifdef __linux__
extern "C" {
#include <sys/ioctl.h>
#include <linux/fd.h>
}
#endif
#include "bochs.h"
// windows.h included by bochs.h
#ifdef WIN32
extern "C" {
#include <winioctl.h>
}
#endif
#define LOG_THIS theFloppyController->

bx_floppy_ctrl_c *theFloppyController;

/* for main status register */
#define FD_MS_MRQ  0x80
#define FD_MS_DIO  0x40
#define FD_MS_NDMA 0x20
#define FD_MS_BUSY 0x10
#define FD_MS_ACTD 0x08
#define FD_MS_ACTC 0x04
#define FD_MS_ACTB 0x02
#define FD_MS_ACTA 0x01

#define FROM_FLOPPY 10
#define TO_FLOPPY   11

#define FLOPPY_DMA_CHAN 2

typedef struct {
  unsigned id;
  Bit8u trk;
  Bit8u hd;
  Bit8u spt;
  unsigned sectors;
} floppy_type_t;

static floppy_type_t floppy_type[8] = {
  {BX_FLOPPY_160K, 40, 1, 8, 320},
  {BX_FLOPPY_180K, 40, 1, 9, 360},
  {BX_FLOPPY_320K, 40, 2, 8, 640},
  {BX_FLOPPY_360K, 40, 2, 9, 720},
  {BX_FLOPPY_720K, 80, 2, 9, 1440},
  {BX_FLOPPY_1_2,  80, 2, 15, 2400},
  {BX_FLOPPY_1_44, 80, 2, 18, 2880},
  {BX_FLOPPY_2_88, 80, 2, 36, 5760}
};


  int
libfloppy_LTX_plugin_init(plugin_t *plugin, plugintype_t type, int argc, char *argv[])
{
  theFloppyController = new bx_floppy_ctrl_c ();
  bx_devices.pluginFloppyDevice = theFloppyController;
  BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theFloppyController, BX_PLUGIN_FLOPPY);
  return(0); // Success
}

  void
libfloppy_LTX_plugin_fini(void)
{
}


bx_floppy_ctrl_c::bx_floppy_ctrl_c(void)
{
  put("FDD");
  settype(FDLOG);
  s.floppy_timer_index = BX_NULL_TIMER_HANDLE;
}

bx_floppy_ctrl_c::~bx_floppy_ctrl_c(void)
{
  // nothing for now
  BX_DEBUG(("Exit."));
}


  void
bx_floppy_ctrl_c::init(void)
{
  Bit8u i;

  BX_DEBUG(("Init $Id: floppy.cc,v 1.69 2003/12/18 20:04:49 vruppert Exp $"));
  DEV_dma_register_8bit_channel(2, dma_read, dma_write, "Floppy Drive");
  DEV_register_irq(6, "Floppy Drive");
  for (unsigned addr=0x03F2; addr<=0x03F7; addr++) {
    DEV_register_ioread_handler(this, read_handler, addr, "Floppy Drive", 1);
    DEV_register_iowrite_handler(this, write_handler, addr, "Floppy Drive", 1);
    }


  DEV_cmos_set_reg(0x10, 0x00); /* start out with: no drive 0, no drive 1 */

  BX_FD_THIS s.num_supported_floppies = 0;

  for (i=0; i<4; i++) {
    BX_FD_THIS s.device_type[i] = BX_FLOPPY_NONE;
    BX_FD_THIS s.media[i].type = BX_FLOPPY_NONE;
    }

  //
  // Floppy A setup
  //
  BX_FD_THIS s.media[0].sectors_per_track = 0;
  BX_FD_THIS s.media[0].tracks            = 0;
  BX_FD_THIS s.media[0].heads             = 0;
  BX_FD_THIS s.media[0].sectors           = 0;
  BX_FD_THIS s.media[0].fd = -1;
  BX_FD_THIS s.media_present[0] = 0;
  BX_FD_THIS s.device_type[0] = bx_options.floppya.Odevtype->get ();

  switch (BX_FD_THIS s.device_type[0]) {
    case BX_FLOPPY_NONE:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x00);
      break;
    case BX_FLOPPY_360K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x10);
      break;
    case BX_FLOPPY_1_2:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x20);
      break;
    case BX_FLOPPY_720K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x30);
      break;
    case BX_FLOPPY_1_44:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x40);
      break;
    case BX_FLOPPY_2_88:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x50);
      break;

    // use CMOS reserved types
    case BX_FLOPPY_160K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x60);
      BX_INFO(("WARNING: 1st floppy uses of reserved CMOS floppy drive type 6"));
      break;
    case BX_FLOPPY_180K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x70);
      BX_INFO(("WARNING: 1st floppy uses of reserved CMOS floppy drive type 7"));
      break;
    case BX_FLOPPY_320K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0x0f) | 0x80);
      BX_INFO(("WARNING: 1st floppy uses of reserved CMOS floppy drive type 8"));
      break;

    default:
      BX_PANIC(("unknown floppya type"));
    }
  if (BX_FD_THIS s.device_type[0] != BX_FLOPPY_NONE)
    BX_FD_THIS s.num_supported_floppies++;

  if (bx_options.floppya.Otype->get () != BX_FLOPPY_NONE) {
    if ( bx_options.floppya.Ostatus->get () == BX_INSERTED) {
      if (evaluate_media(bx_options.floppya.Otype->get (), bx_options.floppya.Opath->getptr (),
                   & BX_FD_THIS s.media[0]))
        BX_FD_THIS s.media_present[0] = 1;
      else
        bx_options.floppya.Ostatus->set(BX_EJECTED);
#define MED (BX_FD_THIS s.media[0])
        BX_INFO(("fd0: '%s' ro=%d, h=%d,t=%d,spt=%d", bx_options.floppya.Opath->getptr(),
        MED.write_protected, MED.heads, MED.tracks, MED.sectors_per_track));
#undef MED
      }
    }


  //
  // Floppy B setup
  //
  BX_FD_THIS s.media[1].sectors_per_track = 0;
  BX_FD_THIS s.media[1].tracks            = 0;
  BX_FD_THIS s.media[1].heads             = 0;
  BX_FD_THIS s.media[1].sectors           = 0;
  BX_FD_THIS s.media[1].fd = -1;
  BX_FD_THIS s.media_present[1] = 0;
  BX_FD_THIS s.device_type[1] = bx_options.floppyb.Odevtype->get ();

  switch (BX_FD_THIS s.device_type[1]) {
    case BX_FLOPPY_NONE:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x00);
      break;
    case BX_FLOPPY_360K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x01);
      break;
    case BX_FLOPPY_1_2:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x02);
      break;
    case BX_FLOPPY_720K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x03);
      break;
    case BX_FLOPPY_1_44:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x04);
      break;
    case BX_FLOPPY_2_88:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x05);
      break;

    // use CMOS reserved types
    case BX_FLOPPY_160K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x06);
      BX_INFO(("WARNING: 2nd floppy uses of reserved CMOS floppy drive type 6"));
      break;
    case BX_FLOPPY_180K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x07);
      BX_INFO(("WARNING: 2nd floppy uses of reserved CMOS floppy drive type 7"));
      break;
    case BX_FLOPPY_320K:
      DEV_cmos_set_reg(0x10, (DEV_cmos_get_reg(0x10) & 0xf0) | 0x08);
      BX_INFO(("WARNING: 2nd floppy uses of reserved CMOS floppy drive type 8"));
      break;

    default:
      BX_PANIC(("unknown floppyb type"));
    }
  if (BX_FD_THIS s.device_type[1] != BX_FLOPPY_NONE)
    BX_FD_THIS s.num_supported_floppies++;

  if (bx_options.floppyb.Otype->get () != BX_FLOPPY_NONE) {
    if ( bx_options.floppyb.Ostatus->get () == BX_INSERTED) {
      if (evaluate_media(bx_options.floppyb.Otype->get (), bx_options.floppyb.Opath->getptr (),
                   & BX_FD_THIS s.media[1]))
        BX_FD_THIS s.media_present[1] = 1;
      else
        bx_options.floppyb.Ostatus->set(BX_EJECTED);
#define MED (BX_FD_THIS s.media[1])
        BX_INFO(("fd1: '%s' ro=%d, h=%d,t=%d,spt=%d", bx_options.floppyb.Opath->getptr(),
        MED.write_protected, MED.heads, MED.tracks, MED.sectors_per_track));
#undef MED
      }
    }



  /* CMOS Equipment Byte register */
  if (BX_FD_THIS s.num_supported_floppies > 0) {
    DEV_cmos_set_reg(0x14, (DEV_cmos_get_reg(0x14) & 0x3e) |
                          ((BX_FD_THIS s.num_supported_floppies-1) << 6) | 1);
  }
  else
    DEV_cmos_set_reg(0x14, (DEV_cmos_get_reg(0x14) & 0x3e));


  if (BX_FD_THIS s.floppy_timer_index == BX_NULL_TIMER_HANDLE) {
    BX_FD_THIS s.floppy_timer_index =
      bx_pc_system.register_timer( this, timer_handler,
      bx_options.Ofloppy_command_delay->get (), 0,0, "floppy");
  }

  BX_DEBUG(("bx_options.Ofloppy_command_delay = %u",
    (unsigned) bx_options.Ofloppy_command_delay->get ()));
}



  void
bx_floppy_ctrl_c::reset(unsigned type)
{
  Bit32u i;

  BX_FD_THIS s.pending_irq = 0;
  BX_FD_THIS s.reset_sensei = 0; /* no reset result present */

  BX_FD_THIS s.main_status_reg = 0;
  BX_FD_THIS s.status_reg0 = 0;
  BX_FD_THIS s.status_reg1 = 0;
  BX_FD_THIS s.status_reg2 = 0;
  BX_FD_THIS s.status_reg3 = 0;

  // software reset (via DOR port 0x3f2 bit 2) does not change DOR
  if (type == BX_RESET_HARDWARE) {
    BX_FD_THIS s.DOR = 0x0c;
    // motor off, drive 3..0
    // DMA/INT enabled
    // normal operation
    // drive select 0

    // DIR and CCR affected only by hard reset
    for (i=0; i<4; i++) {
      BX_FD_THIS s.DIR[i] |= 0x80; // disk changed
      }
    BX_FD_THIS s.data_rate = 0; /* 500 Kbps */
    }

  for (i=0; i<4; i++) {
    BX_FD_THIS s.cylinder[i] = 0;
    BX_FD_THIS s.head[i] = 0;
    BX_FD_THIS s.sector[i] = 0;
    }

  DEV_pic_lower_irq(6);
  DEV_dma_set_drq(FLOPPY_DMA_CHAN, 0);
  enter_idle_phase();
}


  // static IO port read callback handler
  // redirects to non-static class handler to avoid virtual functions

  Bit32u
bx_floppy_ctrl_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
#if !BX_USE_FD_SMF
  bx_floppy_ctrl_c *class_ptr = (bx_floppy_ctrl_c *) this_ptr;

  return( class_ptr->read(address, io_len) );
}


  /* reads from the floppy io ports */
  Bit32u
bx_floppy_ctrl_c::read(Bit32u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif  // !BX_USE_FD_SMF
  Bit8u status, value;

  if (bx_dbg.floppy)
    BX_INFO(("read access to port %04x", (unsigned) address));

  switch (address) {
#if BX_DMA_FLOPPY_IO
    case 0x3F2: // diskette controller digital output register
      value = BX_FD_THIS s.DOR;
      return(value);
      break;

    case 0x3F4: /* diskette controller main status register */
      status = BX_FD_THIS s.main_status_reg;
      return(status);
      break;

    case 0x3F5: /* diskette controller data */
      if (BX_FD_THIS s.result_size == 0) {
        BX_ERROR(("port 0x3f5: no results to read"));
        BX_FD_THIS s.main_status_reg = 0;
        return BX_FD_THIS s.result[0];
        }

      value = BX_FD_THIS s.result[BX_FD_THIS s.result_index++];
      BX_FD_THIS s.main_status_reg &= 0xF0;
      if (BX_FD_THIS s.result_index >= BX_FD_THIS s.result_size) {
        if (!BX_FD_THIS s.reset_sensei) BX_FD_THIS s.pending_irq = 0;
        DEV_pic_lower_irq(6);
        enter_idle_phase();
        }
      return(value);
      break;
#endif  // #if BX_DMA_FLOPPY_IO

    case 0x3F3: // Tape Drive Register
      // see http://www.smsc.com/main/datasheets/37c93x.pdf page 18 for more details

      switch( BX_FD_THIS s.DOR & 0x03 )
      {
        case 0x00:
          if( (BX_FD_THIS s.DOR & 0x10) == 0) break;
          return(2);
        case 0x01:
          if( (BX_FD_THIS s.DOR & 0x20) == 0) break;
          return(1);
      }
      return(3);
      
    case 0x3F6: // Reserved for future floppy controllers
                // This address shared with the hard drive controller
      value = DEV_hd_read_handler(bx_devices.pluginHardDrive, address, io_len);
      return( value );
      break;

    case 0x3F7: // diskette controller digital input register
      // This address shared with the hard drive controller:
      //   Bit  7   : floppy
      //   Bits 6..0: hard drive
      value = DEV_hd_read_handler(bx_devices.pluginHardDrive, address, io_len);
      value &= 0x7f;
      // add in diskette change line
      value |= (BX_FD_THIS s.DIR[BX_FD_THIS s.DOR & 0x03] & 0x80);
      return( value );
      break;
    default:
      BX_ERROR(("io_read: unsupported address 0x%04x", (unsigned) address));
      return(0);
      break;
    }
}


  // static IO port write callback handler
  // redirects to non-static class handler to avoid virtual functions

  void
bx_floppy_ctrl_c::write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
#if !BX_USE_FD_SMF
  bx_floppy_ctrl_c *class_ptr = (bx_floppy_ctrl_c *) this_ptr;

  class_ptr->write(address, value, io_len);
}

  /* writes to the floppy io ports */
  void
bx_floppy_ctrl_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif  // !BX_USE_FD_SMF
  Bit8u dma_and_interrupt_enable;
  Bit8u normal_operation, prev_normal_operation;
  Bit8u drive_select;
  Bit8u motor_on_drive0, motor_on_drive1;

  if (bx_dbg.floppy)
    BX_INFO(("write access to port %04x, value=%02x",
      (unsigned) address, (unsigned) value));

  switch (address) {
#if BX_DMA_FLOPPY_IO
    case 0x3F2: /* diskette controller digital output register */
      motor_on_drive1 = value & 0x20;
      motor_on_drive0 = value & 0x10;
      dma_and_interrupt_enable = value & 0x08;
      if (!dma_and_interrupt_enable)
        BX_DEBUG(("DMA and interrupt capabilities disabled"));
      normal_operation = value & 0x04;
      drive_select = value & 0x03;

      prev_normal_operation = BX_FD_THIS s.DOR & 0x04;
      BX_FD_THIS s.DOR = value;

      if (prev_normal_operation==0 && normal_operation) {
        // transition from RESET to NORMAL
        bx_pc_system.activate_timer( BX_FD_THIS s.floppy_timer_index,
             bx_options.Ofloppy_command_delay->get (), 0 );
        }
      else if (prev_normal_operation && normal_operation==0) {
        // transition from NORMAL to RESET
        BX_FD_THIS s.main_status_reg = FD_MS_BUSY;
        BX_FD_THIS s.pending_command = 0xfe; // RESET pending

        }
        BX_DEBUG(("io_write: digital output register"));
        BX_DEBUG(("  motor on, drive1 = %d", motor_on_drive1 > 0));
        BX_DEBUG(("  motor on, drive0 = %d", motor_on_drive0 > 0));
        BX_DEBUG(("  dma_and_interrupt_enable=%02x",
          (unsigned) dma_and_interrupt_enable));
        BX_DEBUG(("  normal_operation=%02x",
          (unsigned) normal_operation));
        BX_DEBUG(("  drive_select=%02x",
          (unsigned) drive_select));
      if (BX_FD_THIS s.device_type[drive_select] == BX_FLOPPY_NONE) {
        BX_DEBUG(("WARNING: not existing drive selected"));
        }
      break;

    case 0x3f4: /* diskette controller data rate select register */
      BX_ERROR(("io_write: data rate select register unsupported"));
      break;

    case 0x3F5: /* diskette controller data */
      BX_DEBUG(("command = %02x", (unsigned) value));
      if (BX_FD_THIS s.command_complete) {
        if (BX_FD_THIS s.pending_command!=0)
          BX_PANIC(("io: 3f5: receiving new comm, old one (%02x) pending",
            (unsigned) BX_FD_THIS s.pending_command));
        BX_FD_THIS s.command[0] = value;
        BX_FD_THIS s.command_complete = 0;
        BX_FD_THIS s.command_index = 1;
        /* read/write command in progress */
        BX_FD_THIS s.main_status_reg = FD_MS_MRQ | FD_MS_BUSY;
        switch (value) {
          case 0x03: /* specify */
            BX_FD_THIS s.command_size = 3;
            break;
          case 0x04: // get status
            BX_FD_THIS s.command_size = 2;
            break;
          case 0x07: /* recalibrate */
            BX_FD_THIS s.command_size = 2;
            break;
          case 0x08: /* sense interrupt status */
            BX_FD_THIS s.command_size = 1;
            break;
          case 0x0f: /* seek */
            BX_FD_THIS s.command_size = 3;
            break;
          case 0x4a: /* read ID */
            BX_FD_THIS s.command_size = 2;
            break;
          case 0x4d: /* format track */
            BX_FD_THIS s.command_size = 6;
            break;
          case 0x45:
          case 0xc5: /* write normal data */
            BX_FD_THIS s.command_size = 9;
            break;
          case 0x46:
          case 0x66:
          case 0xc6:
          case 0xe6: /* read normal data */
            BX_FD_THIS s.command_size = 9;
            break;

          case 0x13: // Configure command (Enhanced)
            BX_FD_THIS s.command_size = 4;
            break;

          case 0x0e: // dump registers (Enhanced drives)
          case 0x10: // Version command, standard controller returns 80h
          case 0x18: // National Semiconductor version command; return 80h
            // These commands are not implemented on the standard
            // controller and return an error.  They are available on
            // the enhanced controller.
            BX_DEBUG(("io_write: 0x3f5: unsupported floppy command 0x%02x",
              (unsigned) value));
            BX_FD_THIS s.command_size = 0;   // make sure we don't try to process this command
            BX_FD_THIS s.status_reg0 = 0x80; // status: invalid command
            enter_result_phase();
            break;

          default:
            BX_ERROR(("io_write: 0x3f5: invalid floppy command 0x%02x",
              (unsigned) value));
            BX_FD_THIS s.command_size = 0;   // make sure we don't try to process this command
            BX_FD_THIS s.status_reg0 = 0x80; // status: invalid command
            enter_result_phase();
            break;
          }
        }
      else {
        BX_FD_THIS s.command[BX_FD_THIS s.command_index++] =
          value;
        }
      if (BX_FD_THIS s.command_index ==
        BX_FD_THIS s.command_size) {
        /* read/write command not in progress any more */
        floppy_command();
        BX_FD_THIS s.command_complete = 1;
        }
      BX_DEBUG(("io_write: diskette controller data"));
      return;
      break;
#endif  // #if BX_DMA_FLOPPY_IO

    case 0x3F6: /* diskette controller (reserved) */
      BX_DEBUG(("io_write: reserved register 0x3f6 unsupported"));
      // this address shared with the hard drive controller
      DEV_hd_write_handler(bx_devices.pluginHardDrive, address, value, io_len);
      break;

#if BX_DMA_FLOPPY_IO
    case 0x3F7: /* diskette controller configuration control register */
      BX_DEBUG(("io_write: config control register"));
      BX_FD_THIS s.data_rate = value & 0x03;
      switch (BX_FD_THIS s.data_rate) {
        case 0: BX_DEBUG(("  500 Kbps")); break;
        case 1: BX_DEBUG(("  300 Kbps")); break;
        case 2: BX_DEBUG(("  250 Kbps")); break;
        case 3: BX_DEBUG(("  1 Mbps")); break;
      }
      return;
      break;

   default:
      BX_ERROR(("io_write ignored: 0x%04x = 0x%02x", (unsigned) address, (unsigned) value));
      break;
#endif  // #if BX_DMA_FLOPPY_IO
    }
}



  void
bx_floppy_ctrl_c::floppy_command(void)
{
#if BX_PROVIDE_CPU_MEMORY==0
  BX_PANIC(("floppy_command(): uses DMA: not supported for"
           " external environment"));
#else
  unsigned i;
  Bit8u step_rate_time;
  Bit8u head_unload_time;
  Bit8u head_load_time;
  Bit8u motor_on;
  Bit8u head, drive, cylinder, sector, eot;
  Bit8u sector_size, data_length;
  Bit32u logical_sector;


  BX_DEBUG(("FLOPPY COMMAND: "));
  for (i=0; i<BX_FD_THIS s.command_size; i++)
    BX_DEBUG(("[%02x] ", (unsigned) BX_FD_THIS s.command[i]));

#if 0
  /* execute phase of command is in progress (non DMA mode) */
  BX_FD_THIS s.main_status_reg |= 20;
#endif

  BX_FD_THIS s.pending_command = BX_FD_THIS s.command[0];
  switch (BX_FD_THIS s.pending_command) {
    case 0x03: // specify
      // execution: specified parameters are loaded
      // result: no result bytes, no interrupt
      step_rate_time = BX_FD_THIS s.command[1] >> 4;
      head_unload_time = BX_FD_THIS s.command[1] & 0x0f;
      head_load_time = BX_FD_THIS s.command[2] >> 1;
      if (BX_FD_THIS s.command[2] & 0x01)
        BX_ERROR(("non DMA mode selected"));
      enter_idle_phase();
      return;
      break;

    case 0x04: // get status
      drive = (BX_FD_THIS s.command[1] & 0x03);
      BX_FD_THIS s.head[drive] = (BX_FD_THIS s.command[1] >> 2) & 0x01;
      BX_FD_THIS s.status_reg3 = 0x28 | (BX_FD_THIS s.head[drive]<<2) | drive
        | (BX_FD_THIS s.media[drive].write_protected ? 0x40 : 0x00);
      if (BX_FD_THIS s.cylinder[drive] == 0) BX_FD_THIS s.status_reg3 |= 0x10;
      enter_result_phase();
      return;
      break;

    case 0x07: // recalibrate
      drive = (BX_FD_THIS s.command[1] & 0x03);
      BX_FD_THIS s.DOR &= 0xfc;
      BX_FD_THIS s.DOR |= drive;
      BX_DEBUG(("floppy_command(): recalibrate drive %u",
        (unsigned) drive));
      motor_on = ( (BX_FD_THIS s.DOR>>(drive+4))
                     & 0x01 );
      if (motor_on == 0) {
        BX_INFO(("floppy_command(): recal drive with motor off"));
        }
      if (drive==0)
        BX_FD_THIS s.DOR |= 0x10; // turn on MOTA
      else
        BX_FD_THIS s.DOR |= 0x20; // turn on MOTB
      bx_pc_system.activate_timer( BX_FD_THIS s.floppy_timer_index,
        bx_options.Ofloppy_command_delay->get (), 0 );
      /* command head to track 0
       * controller set to non-busy
       * error condition noted in Status reg 0's equipment check bit
       * seek end bit set to 1 in Status reg 0 regardless of outcome
       * The last two are taken care of in timer().
       */
      BX_FD_THIS s.cylinder[drive] = 0;
      BX_FD_THIS s.main_status_reg = (1 << drive);
      return;
      break;

    case 0x08: /* sense interrupt status */
      /* execution:
       *   get status
       * result:
       *   no interupt
       *   byte0 = status reg0
       *   byte1 = current cylinder number (0 to 79)
       */
      drive = BX_FD_THIS s.DOR & 0x03;
      if (!BX_FD_THIS s.pending_irq) {
        BX_FD_THIS s.status_reg0 = 0x80;
        }
      else {
        if (BX_FD_THIS s.reset_sensei > 0) {
          drive = 4 - BX_FD_THIS s.reset_sensei;
          BX_FD_THIS s.status_reg0 &= 0xf8;
          BX_FD_THIS s.status_reg0 |= (BX_FD_THIS s.head[drive] << 2) | drive;
          BX_FD_THIS s.reset_sensei--;
          }
        }

      BX_DEBUG(("sense interrupt status"));
      enter_result_phase();
      return;
      break;

    case 0x0f: /* seek */
      /* command:
       *   byte0 = 0F
       *   byte1 = drive & head select
       *   byte2 = cylinder number
       * execution:
       *   postion head over specified cylinder
       * result:
       *   no result bytes, issues an interrupt
       */
      drive = BX_FD_THIS s.command[1] & 0x03;
      BX_FD_THIS s.DOR &= 0xfc;
      BX_FD_THIS s.DOR |= drive;

      BX_FD_THIS s.head[drive] = (BX_FD_THIS s.command[1] >> 2) & 0x01;
      BX_FD_THIS s.cylinder[drive] = BX_FD_THIS s.command[2];
      /* ??? should also check cylinder validity */
      bx_pc_system.activate_timer( BX_FD_THIS s.floppy_timer_index,
        bx_options.Ofloppy_command_delay->get (), 0 );
      /* data reg not ready, drive busy */
      BX_FD_THIS s.main_status_reg = (1 << drive);
      return;
      break;

    case 0x13: // Configure
      BX_DEBUG(("configure (eis     = 0x%02x)", BX_FD_THIS s.command[2] & 0x40 ));
      BX_DEBUG(("configure (efifo   = 0x%02x)", BX_FD_THIS s.command[2] & 0x20 ));
      BX_DEBUG(("configure (no poll = 0x%02x)", BX_FD_THIS s.command[2] & 0x10 ));
      BX_DEBUG(("configure (fifothr = 0x%02x)", BX_FD_THIS s.command[2] & 0x0f ));
      BX_DEBUG(("configure (pretrk  = 0x%02x)", BX_FD_THIS s.command[3] ));
      enter_idle_phase();
      return;
      break;

    case 0x4a: // read ID
      drive = BX_FD_THIS s.command[1] & 0x03;
      BX_FD_THIS s.head[drive] = (BX_FD_THIS s.command[1] >> 2) & 0x01;
      BX_FD_THIS s.DOR &= 0xfc;
      BX_FD_THIS s.DOR |= drive;

      motor_on = (BX_FD_THIS s.DOR>>(drive+4)) & 0x01;
      if (motor_on == 0) {
        BX_ERROR(("floppy_command(): 0x4a: motor not on"));
        BX_FD_THIS s.main_status_reg = FD_MS_BUSY;
        return;
        }
      if (BX_FD_THIS s.device_type[drive] == BX_FLOPPY_NONE)
        BX_PANIC(("floppy_command(): read ID: bad drive #%d", drive));
      BX_FD_THIS s.status_reg0 = (BX_FD_THIS s.head[drive]<<2) | drive;
      bx_pc_system.activate_timer( BX_FD_THIS s.floppy_timer_index,
        bx_options.Ofloppy_command_delay->get (), 0 );
      /* data reg not ready, controller busy */
      BX_FD_THIS s.main_status_reg = FD_MS_BUSY;
      return;
      break;

    case 0x4d: // format track
        drive = BX_FD_THIS s.command[1] & 0x03;
        BX_FD_THIS s.DOR &= 0xfc;
        BX_FD_THIS s.DOR |= drive;

        motor_on = (BX_FD_THIS s.DOR>>(drive+4)) & 0x01;
        if (motor_on == 0)
          BX_PANIC(("floppy_command(): format track: motor not on"));
        BX_FD_THIS s.head[drive] = (BX_FD_THIS s.command[1] >> 2) & 0x01;
        sector_size = BX_FD_THIS s.command[2];
        BX_FD_THIS s.format_count = BX_FD_THIS s.command[3];
        BX_FD_THIS s.format_fillbyte = BX_FD_THIS s.command[5];
        if (BX_FD_THIS s.device_type[drive] == BX_FLOPPY_NONE)
          BX_PANIC(("floppy_command(): format track: bad drive #%d", drive));

        if (sector_size != 0x02) { // 512 bytes
          BX_PANIC(("format track: sector size %d not supported", 128<<sector_size));
          }
        if (BX_FD_THIS s.format_count != BX_FD_THIS s.media[drive].sectors_per_track) {
          BX_PANIC(("format track: %d sectors/track requested (%d expected)",
                    BX_FD_THIS s.format_count, BX_FD_THIS s.media[drive].sectors_per_track));
          }
        if ( BX_FD_THIS s.media_present[drive] == 0 ) {
          // media not in drive, return error
          BX_INFO(("attempt to format track with media not present"));
          BX_FD_THIS s.status_reg0 = 0x40 | (BX_FD_THIS s.head[drive]<<2) | drive; // abnormal termination
          BX_FD_THIS s.status_reg1 = 0x25; // 0010 0101
          BX_FD_THIS s.status_reg2 = 0x31; // 0011 0001
          enter_result_phase();
          return;
          }
        if (BX_FD_THIS s.media[drive].write_protected) {
          // media write-protected, return error
          BX_INFO(("attempt to format track with media write-protected"));
          BX_FD_THIS s.status_reg0 = 0x40 | (BX_FD_THIS s.head[drive]<<2) | drive; // abnormal termination
          BX_FD_THIS s.status_reg1 = 0x27; // 0010 0111
          BX_FD_THIS s.status_reg2 = 0x31; // 0011 0001
          enter_result_phase();
          return;
          }

      /* 4 header bytes per sector are required */
      BX_FD_THIS s.format_count <<= 2;

      DEV_dma_set_drq(FLOPPY_DMA_CHAN, 1);

      /* data reg not ready, controller busy */
      BX_FD_THIS s.main_status_reg = FD_MS_BUSY;
      BX_DEBUG(("format track"));
      return;
      break;

    case 0x46: // read normal data, MT=0, SK=0
    case 0x66: // read normal data, MT=0, SK=1
    case 0xc6: // read normal data, MT=1, SK=0
    case 0xe6: // read normal data, MT=1, SK=1
    case 0x45: // write normal data, MT=0
    case 0xc5: // write normal data, MT=1
      BX_FD_THIS s.multi_track = (BX_FD_THIS s.command[0] >> 7);
      if ( (BX_FD_THIS s.DOR & 0x08) == 0 )
        BX_PANIC(("read/write command with DMA and int disabled"));
      drive = BX_FD_THIS s.command[1] & 0x03;
      BX_FD_THIS s.DOR &= 0xfc;
      BX_FD_THIS s.DOR |= drive;

      motor_on = (BX_FD_THIS s.DOR>>(drive+4)) & 0x01;
      if (motor_on == 0)
        BX_PANIC(("floppy_command(): read/write: motor not on"));
      head = BX_FD_THIS s.command[3] & 0x01;
      cylinder = BX_FD_THIS s.command[2]; /* 0..79 depending */
      sector = BX_FD_THIS s.command[4];   /* 1..36 depending */
      eot = BX_FD_THIS s.command[6];      /* 1..36 depending */
      sector_size = BX_FD_THIS s.command[5];
      data_length = BX_FD_THIS s.command[8];
      BX_DEBUG(("read/write normal data"));
      BX_DEBUG(("BEFORE"));
      BX_DEBUG(("  drive    = %u", (unsigned) drive));
      BX_DEBUG(("  head     = %u", (unsigned) head));
      BX_DEBUG(("  cylinder = %u", (unsigned) cylinder));
      BX_DEBUG(("  sector   = %u", (unsigned) sector));
      BX_DEBUG(("  eot      = %u", (unsigned) eot));
      if (BX_FD_THIS s.device_type[drive] == BX_FLOPPY_NONE)
        BX_PANIC(("floppy_command(): read/write: bad drive #%d", drive));

      // check that head number in command[1] bit two matches the head
      // reported in the head number field.  Real floppy drives are
      // picky about this, as reported in SF bug #439945, (Floppy drive
      // read input error checking).
      if (head != ((BX_FD_THIS s.command[1]>>2)&1)) {
        BX_ERROR(("head number in command[1] doesn't match head field"));
        BX_FD_THIS s.status_reg0 = 0x40 | (BX_FD_THIS s.head[drive]<<2) | drive; // abnormal termination
        BX_FD_THIS s.status_reg1 = 0x04; // 0000 0100
        BX_FD_THIS s.status_reg2 = 0x00; // 0000 0000
        enter_result_phase();
        return;
      }

      if ( BX_FD_THIS s.media_present[drive] == 0 ) {
        // media not in drive, return error

        BX_INFO(("attempt to read/write sector %u,"
                     " sectors/track=%u with media not present", 
                     (unsigned) sector,
                     (unsigned) BX_FD_THIS s.media[drive].sectors_per_track));
        BX_FD_THIS s.status_reg0 = 0x40 | (BX_FD_THIS s.head[drive]<<2) | drive; // abnormal termination
        BX_FD_THIS s.status_reg1 = 0x25; // 0010 0101
        BX_FD_THIS s.status_reg2 = 0x31; // 0011 0001
        enter_result_phase();
        return;
        }

      if (sector_size != 0x02) { // 512 bytes
        BX_PANIC(("read/write command: sector size %d not supported", 128<<sector_size));
        }
      if ( cylinder >= BX_FD_THIS s.media[drive].tracks ) {
        BX_PANIC(("io: norm r/w parms out of range: sec#%02xh cyl#%02xh eot#%02xh head#%02xh",
          (unsigned) sector, (unsigned) cylinder, (unsigned) eot,
          (unsigned) head));
        return;
      }

      if (sector > BX_FD_THIS s.media[drive].sectors_per_track) {
        // requested sector > last sector on track
        BX_INFO(("attempt to read/write sector %u,"
                     " sectors/track=%u", (unsigned) sector,
                     (unsigned) BX_FD_THIS s.media[drive].sectors_per_track));
        // set controller to where drive would have left off
        // after it discovered the sector was past EOT
        BX_FD_THIS s.cylinder[drive] = cylinder;
        BX_FD_THIS s.head[drive]     = head;
        BX_FD_THIS s.sector[drive]   = BX_FD_THIS s.media[drive].sectors_per_track;

        // 0100 0HDD abnormal termination
        BX_FD_THIS s.status_reg0 = 0x40 | (BX_FD_THIS s.head[drive]<<2) | drive;
        // 1000 0101 end of cyl/NDAT/NID
        BX_FD_THIS s.status_reg1 = 0x85;
        // 0000 0000
        BX_FD_THIS s.status_reg2 = 0x00;
        enter_result_phase();
        return;
        }

      if (cylinder != BX_FD_THIS s.cylinder[drive])
        BX_DEBUG(("io: cylinder request != current cylinder"));

        // original assumed all floppies had two sides...now it does not  *delete this comment line*
        logical_sector = (cylinder * BX_FD_THIS s.media[drive].heads * BX_FD_THIS s.media[drive].sectors_per_track) +
                       (head * BX_FD_THIS s.media[drive].sectors_per_track) +
                       (sector - 1);

      if (logical_sector >= BX_FD_THIS s.media[drive].sectors) {
        BX_PANIC(("io: logical sector out of bounds"));
        }

      BX_FD_THIS s.cylinder[drive] = cylinder;
      BX_FD_THIS s.sector[drive]   = sector;
      BX_FD_THIS s.head[drive]     = head;

      if ((BX_FD_THIS s.command[0] & 0x4f) == 0x46) { // read
        floppy_xfer(drive, logical_sector*512, BX_FD_THIS s.floppy_buffer,
                    512, FROM_FLOPPY);

        DEV_dma_set_drq(FLOPPY_DMA_CHAN, 1);

        /* data reg not ready, controller busy */
        BX_FD_THIS s.main_status_reg = FD_MS_BUSY;
        return;
        }
      else if ((BX_FD_THIS s.command[0] & 0x7f) == 0x45) { // write

        DEV_dma_set_drq(FLOPPY_DMA_CHAN, 1);

        /* data reg not ready, controller busy */
        BX_FD_THIS s.main_status_reg = FD_MS_BUSY;
        return;
        }
      else
        BX_PANIC(("floppy_command(): unknown read/write command"));

      return;
      break;

    default: // invalid or unsupported command; these are captured in write() above
      BX_PANIC(("You should never get here! cmd = 0x%02x", 
                BX_FD_THIS s.command[0]));
    }
#endif
}

  void
bx_floppy_ctrl_c::floppy_xfer(Bit8u drive, Bit32u offset, Bit8u *buffer,
            Bit32u bytes, Bit8u direction)
{
  int ret;

  if (BX_FD_THIS s.device_type[drive] == BX_FLOPPY_NONE)
    BX_PANIC(("floppy_xfer: bad drive #%d", drive));

  if (bx_dbg.floppy) {
    BX_INFO(("drive=%u", (unsigned) drive));
    BX_INFO(("offset=%u", (unsigned) offset));
    BX_INFO(("bytes=%u", (unsigned) bytes));
    BX_INFO(("direction=%s", (direction==FROM_FLOPPY)? "from" : "to"));
    }

#if BX_WITH_MACOS
  if (strcmp(bx_options.floppya.Opath->getptr (), SuperDrive))
#endif
    {
    ret = lseek(BX_FD_THIS s.media[drive].fd, offset, SEEK_SET);
    if (ret < 0) {
      BX_PANIC(("could not perform lseek() on floppy image file"));
      }
    }

  if (direction == FROM_FLOPPY) {
#if BX_WITH_MACOS
    if (!strcmp(bx_options.floppya.Opath->getptr (), SuperDrive))
      ret = fd_read((char *) buffer, offset, bytes);
    else
#endif
      ret = ::read(BX_FD_THIS s.media[drive].fd, (bx_ptr_t) buffer, bytes);
    if (ret < int(bytes)) {
      /* ??? */
      if (ret > 0) {
        BX_INFO(("partial read() on floppy image returns %u/%u",
          (unsigned) ret, (unsigned) bytes));
        memset(buffer + ret, 0, bytes - ret);
        }
      else {
        BX_INFO(("read() on floppy image returns 0"));
        memset(buffer, 0, bytes);
        }
      }
    }

  else { // TO_FLOPPY
    BX_ASSERT (!BX_FD_THIS s.media[drive].write_protected);
#if BX_WITH_MACOS
    if (!strcmp(bx_options.floppya.Opath->getptr (), SuperDrive))
      ret = fd_write((char *) buffer, offset, bytes);
    else
#endif
      ret = ::write(BX_FD_THIS s.media[drive].fd, (bx_ptr_t) buffer, bytes);
    if (ret < int(bytes)) {
      BX_PANIC(("could not perform write() on floppy image file"));
    }
  }
}



  void
bx_floppy_ctrl_c::timer_handler(void *this_ptr)
{

  bx_floppy_ctrl_c *class_ptr = (bx_floppy_ctrl_c *) this_ptr;

  class_ptr->timer();
}

  void
bx_floppy_ctrl_c::timer()
{
  Bit8u drive;

  drive = BX_FD_THIS s.DOR & 0x03;
  switch ( BX_FD_THIS s.pending_command ) {
    case 0x07: // recal
    case 0x0f: // seek
      BX_FD_THIS s.status_reg0 = 0x20 | (BX_FD_THIS s.head[drive]<<2) | drive;
      if (BX_FD_THIS s.device_type[drive] == BX_FLOPPY_NONE) {
        BX_FD_THIS s.status_reg0 |= 0x50;
        }
      else if (BX_FD_THIS s.media_present[drive] == 0) {
        BX_FD_THIS s.status_reg0 |= 0x40;
        BX_FD_THIS s.status_reg1 = 0x25;
        BX_FD_THIS s.status_reg2 = 0x31;
        }

      /* reset changeline */
      if (drive > 1) return;
      if (BX_FD_THIS s.media_present[drive])
        BX_FD_THIS s.DIR[drive] &= ~0x80; // clear disk change line

      enter_idle_phase();
      raise_interrupt();
      break;

    case 0x4a: /* read ID */
      enter_result_phase();
      break;

    case 0xfe: // (contrived) RESET
      theFloppyController->reset(BX_RESET_SOFTWARE);
      BX_FD_THIS s.pending_command = 0;
      BX_FD_THIS s.status_reg0 = 0xc0;
      raise_interrupt();
      BX_FD_THIS s.reset_sensei = 4;
      break;
    
    case 0x00: // nothing pending?
      break;

    default:
      BX_PANIC(("floppy:timer(): unknown case %02x",
        (unsigned) BX_FD_THIS s.pending_command));
    }
  return;
}

  void
bx_floppy_ctrl_c::dma_write(Bit8u *data_byte)
{
  // A DMA write is from I/O to Memory
  // We need to return then next data byte from the floppy buffer
  // to be transfered via the DMA to memory. (read block from floppy)


  *data_byte = BX_FD_THIS s.floppy_buffer[BX_FD_THIS s.floppy_buffer_index++];

  if (BX_FD_THIS s.floppy_buffer_index >= 512) {
    Bit8u drive;

    drive = BX_FD_THIS s.DOR & 0x03;
    increment_sector(); // increment to next sector before retrieving next one
    BX_FD_THIS s.floppy_buffer_index = 0;
    if (DEV_dma_get_tc()) { // Terminal Count line, done
      BX_FD_THIS s.status_reg0 = (BX_FD_THIS s.head[drive] << 2) | drive;
      BX_FD_THIS s.status_reg1 = 0;
      BX_FD_THIS s.status_reg2 = 0;

      if (bx_dbg.floppy) {
        BX_INFO(("<<READ DONE>>"));
        BX_INFO(("AFTER"));
        BX_INFO(("  drive    = %u", (unsigned) drive));
        BX_INFO(("  head     = %u", (unsigned) BX_FD_THIS s.head[drive]));
        BX_INFO(("  cylinder = %u", (unsigned) BX_FD_THIS s.cylinder[drive]));
        BX_INFO(("  sector   = %u", (unsigned) BX_FD_THIS s.sector[drive]));
        }

      DEV_dma_set_drq(FLOPPY_DMA_CHAN, 0);
      enter_result_phase();
      }
    else { // more data to transfer
      Bit32u logical_sector;

      // original assumed all floppies had two sides...now it does not  *delete this comment line*
      logical_sector = (BX_FD_THIS s.cylinder[drive] * BX_FD_THIS s.media[drive].heads *
                        BX_FD_THIS s.media[drive].sectors_per_track) +
                       (BX_FD_THIS s.head[drive] *
                        BX_FD_THIS s.media[drive].sectors_per_track) +
                       (BX_FD_THIS s.sector[drive] - 1);

      floppy_xfer(drive, logical_sector*512, BX_FD_THIS s.floppy_buffer,
                  512, FROM_FLOPPY);
      }
    }
}

  void
bx_floppy_ctrl_c::dma_read(Bit8u *data_byte)
{
  // A DMA read is from Memory to I/O
  // We need to write the data_byte which was already transfered from memory
  // via DMA to I/O (write block to floppy)

  Bit8u drive;
  Bit32u logical_sector;

  drive = BX_FD_THIS s.DOR & 0x03;
  if (BX_FD_THIS s.pending_command == 0x4d) { // format track in progress
    --BX_FD_THIS s.format_count;
    switch (3 - (BX_FD_THIS s.format_count & 0x03)) {
      case 0:
        BX_FD_THIS s.cylinder[drive] = *data_byte;
        break;
      case 1:
        if (*data_byte != BX_FD_THIS s.head[drive])
          BX_ERROR(("head number does not match head field"));
        break;
      case 2:
        BX_FD_THIS s.sector[drive] = *data_byte;
        break;
      case 3:
        if (*data_byte != 2) BX_ERROR(("dma_read: sector size %d not supported", 128<<(*data_byte)));
        BX_DEBUG(("formatting cylinder %u head %u sector %u",
                  BX_FD_THIS s.cylinder[drive], BX_FD_THIS s.head[drive],
                  BX_FD_THIS s.sector[drive]));
        for (unsigned i = 0; i < 512; i++) {
          BX_FD_THIS s.floppy_buffer[i] = BX_FD_THIS s.format_fillbyte;
          }
        // original assumed all floppies had two sides...now it does not *delete this comment line*
        logical_sector = (BX_FD_THIS s.cylinder[drive] * BX_FD_THIS s.media[drive].heads * BX_FD_THIS s.media[drive].sectors_per_track) +
                         (BX_FD_THIS s.head[drive] * BX_FD_THIS s.media[drive].sectors_per_track) +
                         (BX_FD_THIS s.sector[drive] - 1);
        floppy_xfer(drive, logical_sector*512, BX_FD_THIS s.floppy_buffer,
                    512, TO_FLOPPY);
        break;
      }
    if ((BX_FD_THIS s.format_count == 0) || (DEV_dma_get_tc())) {
      BX_FD_THIS s.format_count = 0;
      BX_FD_THIS s.status_reg0 = (BX_FD_THIS s.head[drive] << 2) | drive;
      DEV_dma_set_drq(FLOPPY_DMA_CHAN, 0);
      enter_result_phase();
      }
    return;
    }

  BX_FD_THIS s.floppy_buffer[BX_FD_THIS s.floppy_buffer_index++] = *data_byte;

  if (BX_FD_THIS s.floppy_buffer_index >= 512) {
    // original assumed all floppies had two sides...now it does not *delete this comment line*
    logical_sector = (BX_FD_THIS s.cylinder[drive] * BX_FD_THIS s.media[drive].heads * BX_FD_THIS s.media[drive].sectors_per_track) +
                     (BX_FD_THIS s.head[drive] * BX_FD_THIS s.media[drive].sectors_per_track) +
                     (BX_FD_THIS s.sector[drive] - 1);
  if ( BX_FD_THIS s.media[drive].write_protected ) {
    // write protected error
    BX_INFO(("tried to write disk %u, which is write-protected", drive));
    // ST0: IC1,0=01  (abnormal termination: started execution but failed)
    BX_FD_THIS s.status_reg0 = 0x40 | (BX_FD_THIS s.head[drive]<<2) | drive;
    // ST1: DataError=1, NDAT=1, NotWritable=1, NID=1
    BX_FD_THIS s.status_reg1 = 0x27; // 0010 0111
    // ST2: CRCE=1, SERR=1, BCYL=1, NDAM=1.
    BX_FD_THIS s.status_reg2 = 0x31; // 0011 0001
    enter_result_phase();
    return;
    }
    floppy_xfer(drive, logical_sector*512, BX_FD_THIS s.floppy_buffer,
                512, TO_FLOPPY);
    increment_sector(); // increment to next sector after writing current one
    BX_FD_THIS s.floppy_buffer_index = 0;
    if (DEV_dma_get_tc()) { // Terminal Count line, done
      BX_FD_THIS s.status_reg0 = (BX_FD_THIS s.head[drive] << 2) | drive;
      BX_FD_THIS s.status_reg1 = 0;
      BX_FD_THIS s.status_reg2 = 0;

      if (bx_dbg.floppy) {
        BX_INFO(("<<WRITE DONE>>"));
        BX_INFO(("AFTER"));
        BX_INFO(("  drive    = %u", (unsigned) drive));
        BX_INFO(("  head     = %u", (unsigned) BX_FD_THIS s.head[drive]));
        BX_INFO(("  cylinder = %u", (unsigned) BX_FD_THIS s.cylinder[drive]));
        BX_INFO(("  sector   = %u", (unsigned) BX_FD_THIS s.sector[drive]));
        }

      DEV_dma_set_drq(FLOPPY_DMA_CHAN, 0);
      enter_result_phase();
      }
    else { // more data to transfer
      } // else
    } // if BX_FD_THIS s.floppy_buffer_index >= 512
}

  void
bx_floppy_ctrl_c::raise_interrupt(void)
{
  DEV_pic_raise_irq(6);
  BX_FD_THIS s.pending_irq = 1;
  BX_FD_THIS s.reset_sensei = 0;
}


  void
bx_floppy_ctrl_c::increment_sector(void)
{
  Bit8u drive;

  drive = BX_FD_THIS s.DOR & 0x03;

  // values after completion of data xfer
  // ??? calculation depends on base_count being multiple of 512
  BX_FD_THIS s.sector[drive] ++;
  if (BX_FD_THIS s.sector[drive] > BX_FD_THIS s.media[drive].sectors_per_track) {
    BX_FD_THIS s.sector[drive] = 1;
    if (BX_FD_THIS s.multi_track) {
      BX_FD_THIS s.head[drive] ++;
      if (BX_FD_THIS s.head[drive] > 1) {
        BX_FD_THIS s.head[drive] = 0;
        BX_FD_THIS s.cylinder[drive] ++;
        }
      }
    else {
      BX_FD_THIS s.cylinder[drive] ++;
      }
    if (BX_FD_THIS s.cylinder[drive] >= BX_FD_THIS s.media[drive].tracks) {
      // Set to 1 past last possible cylinder value.
      // I notice if I set it to tracks-1, prama linux won't boot.
      BX_FD_THIS s.cylinder[drive] = BX_FD_THIS s.media[drive].tracks;
      BX_INFO(("increment_sector: clamping cylinder to max"));
      }
    }
}

  unsigned
bx_floppy_ctrl_c::set_media_status(unsigned drive, unsigned status)
{
  char *path;
  unsigned type;

  if (drive == 0)
    type = bx_options.floppya.Otype->get ();
  else
    type = bx_options.floppyb.Otype->get ();

  // if setting to the current value, nothing to do
  if ((status == BX_FD_THIS s.media_present[drive]) &&
      ((status == 0) || (type == BX_FD_THIS s.media[drive].type)))
    return(status);

  if (status == 0) {
    // eject floppy
    if (BX_FD_THIS s.media[drive].fd >= 0) {
      close( BX_FD_THIS s.media[drive].fd );
      BX_FD_THIS s.media[drive].fd = -1;
      }
    BX_FD_THIS s.media_present[drive] = 0;
    if (drive == 0) {
      bx_options.floppya.Ostatus->set(BX_EJECTED);
    } else {
      bx_options.floppyb.Ostatus->set(BX_EJECTED);
    }
    BX_FD_THIS s.DIR[drive] |= 0x80; // disk changed line
    return(0);
    }
  else {
    // insert floppy
    if (drive == 0) {
      path = bx_options.floppya.Opath->getptr ();
      }
    else {
      path = bx_options.floppyb.Opath->getptr ();
      }
    if (!strcmp(path, "none"))
      return(0);
    if (evaluate_media(type, path, & BX_FD_THIS s.media[drive])) {
      BX_FD_THIS s.media_present[drive] = 1;
      if (drive == 0) {
#define MED (BX_FD_THIS s.media[0])
        BX_INFO(("fd0: '%s' ro=%d, h=%d,t=%d,spt=%d", bx_options.floppya.Opath->getptr(),
        MED.write_protected, MED.heads, MED.tracks, MED.sectors_per_track));
#undef MED
        bx_options.floppya.Ostatus->set(BX_INSERTED);
      } else {
#define MED (BX_FD_THIS s.media[1])
        BX_INFO(("fd1: '%s' ro=%d, h=%d,t=%d,spt=%d", bx_options.floppyb.Opath->getptr(),
        MED.write_protected, MED.heads, MED.tracks, MED.sectors_per_track));
#undef MED
        bx_options.floppyb.Ostatus->set(BX_INSERTED);
      }
      BX_FD_THIS s.DIR[drive] |= 0x80; // disk changed line
      return(1);
      }
    else {
      BX_FD_THIS s.media_present[drive] = 0;
      if (drive == 0) {
        bx_options.floppya.Ostatus->set(BX_EJECTED);
      } else {
        bx_options.floppyb.Ostatus->set(BX_EJECTED);
      }
      return(0);
      }
    }
}

  unsigned
bx_floppy_ctrl_c::get_media_status(unsigned drive)
{
  return( BX_FD_THIS s.media_present[drive] );
}

#ifdef O_BINARY
#define BX_RDONLY O_RDONLY | O_BINARY
#define BX_RDWR O_RDWR | O_BINARY
#else
#define BX_RDONLY O_RDONLY
#define BX_RDWR O_RDWR
#endif

  bx_bool
bx_floppy_ctrl_c::evaluate_media(unsigned type, char *path, floppy_t *media)
{
  struct stat stat_buf;
  int i, ret;
  int idx = -1;
#ifdef __linux__
  struct floppy_struct floppy_geom;
#endif
#ifdef WIN32
  char sTemp[1024];
  bx_bool raw_floppy = 0;
  HANDLE hFile;
  DWORD bytes;
  DISK_GEOMETRY dg;
  unsigned tracks, heads, spt;
#endif

  if (type == BX_FLOPPY_NONE)
    return(0);

  //If media file is already open, close it before reopening.
  if(media->fd >=0) {
    close(media->fd);
    media->fd=-1;
  }

  // open media file (image file or device)
  media->write_protected = 0;
#ifdef macintosh
  media->fd = 0;
  if (strcmp(bx_options.floppya.Opath->getptr (), SuperDrive))
#endif
#ifdef WIN32
    if ( (isalpha(path[0])) && (path[1] == ':') && (strlen(path) == 2) ) {
      raw_floppy = 1;
      wsprintf(sTemp, "\\\\.\\%s", path);
      hFile = CreateFile(sTemp, GENERIC_READ, FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile == INVALID_HANDLE_VALUE) {
        BX_ERROR(("Cannot open floppy drive"));
        return(0);
      } else {
        if (!DeviceIoControl(hFile, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg), &bytes, NULL)) {
          BX_ERROR(("No media in floppy drive"));
          CloseHandle(hFile);
          return(0);
        } else {
          tracks = (unsigned)dg.Cylinders.QuadPart;
          heads  = (unsigned)dg.TracksPerCylinder;
          spt    = (unsigned)dg.SectorsPerTrack;
        }
        CloseHandle(hFile);
      }
      media->fd = open(sTemp, BX_RDWR);
    } else {
      media->fd = open(path, BX_RDWR);
    } 
#else
    media->fd = open(path, BX_RDWR);
#endif

  if (media->fd < 0) {
    BX_INFO(( "tried to open '%s' read/write: %s",path,strerror(errno) ));
    // try opening the file read-only
    media->write_protected = 1;
#ifdef macintosh
  media->fd = 0;
  if (strcmp(bx_options.floppya.Opath->getptr (), SuperDrive))
#endif
#ifdef WIN32
    if (raw_floppy == 1) {
      media->fd = open(sTemp, BX_RDONLY);
    } else {
      media->fd = open(path, BX_RDONLY);
    }
#else
    media->fd = open(path, BX_RDONLY);
#endif
    if (media->fd < 0) {
      // failed to open read-only too
      BX_INFO(( "tried to open '%s' read only: %s",path,strerror(errno) ));
      media->type = type;
      return(0);
    }
  }

#if BX_WITH_MACOS
  if (!strcmp(bx_options.floppya.Opath->getptr (), SuperDrive))
    ret = fd_stat(&stat_buf);
  else
    ret = fstat(media->fd, &stat_buf);
#elif defined(WIN32)
  if (raw_floppy) {
    memset (&stat_buf, 0, sizeof(stat_buf));
    stat_buf.st_mode = S_IFCHR;
    ret = 0;
  } else {
    ret = fstat(media->fd, &stat_buf);
  }
#else
  // unix
  ret = fstat(media->fd, &stat_buf);
#endif
  if (ret) {
    BX_PANIC(("fstat floppy 0 drive image file returns error: %s", strerror(errno)));
    return(0);
    }

  for (i = 0; i < 8; i++) {
    if (type == floppy_type[i].id) idx = i;
  }
  if (idx == -1 ) {
    BX_PANIC(("evaluate_media: unknown media type"));
    return(0);
  }
  if ( S_ISREG(stat_buf.st_mode) ) {
    // regular file
    switch (type) {
      // use CMOS reserved types
      case BX_FLOPPY_160K: // 160K 5.25"
      case BX_FLOPPY_180K: // 180K 5.25"
      case BX_FLOPPY_320K: // 320K 5.25"
      // standard floppy types
      case BX_FLOPPY_360K: // 360K 5.25"
      case BX_FLOPPY_720K: // 720K 3.5"
      case BX_FLOPPY_1_2: // 1.2M 5.25"
      case BX_FLOPPY_2_88: // 2.88M 3.5"
        media->type              = type;
        media->tracks            = floppy_type[idx].trk;
        media->heads             = floppy_type[idx].hd;
        media->sectors_per_track = floppy_type[idx].spt;
        media->sectors           = floppy_type[idx].sectors;
        if (stat_buf.st_size > (media->sectors * 512)) {
          BX_INFO(("evaluate_media: size of file '%s' (%lu) too large for selected type",
                   path, (unsigned long) stat_buf.st_size));
          return(0);
        }
        break;
      default: // 1.44M 3.5"
        media->type              = type;
        if (stat_buf.st_size <= 1474560) {
          media->tracks            = floppy_type[idx].trk;
          media->heads             = floppy_type[idx].hd;
          media->sectors_per_track = floppy_type[idx].spt;
          }
        else if (stat_buf.st_size == 1720320) {
          media->sectors_per_track = 21;
          media->tracks            = 80;
          media->heads             = 2;
          }
        else if (stat_buf.st_size == 1763328) {
          media->sectors_per_track = 21;
          media->tracks            = 82;
          media->heads             = 2;
          }
        else {
          BX_INFO(("evaluate_media: file '%s' of unknown size %lu",
            path, (unsigned long) stat_buf.st_size));
          return(0);
          }
        media->sectors = media->heads * media->tracks * media->sectors_per_track;
      }
    return(1); // success
    }

  else if ( S_ISCHR(stat_buf.st_mode)
#if BX_WITH_MACOS == 0
#ifdef S_ISBLK
            || S_ISBLK(stat_buf.st_mode)
#endif
#endif
           ) {
    // character or block device
    // assume media is formatted to typical geometry for drive
    media->type              = type;
#ifdef __linux__
    if (ioctl(media->fd, FDGETPRM, &floppy_geom) < 0) {
      BX_ERROR(("cannot determine media geometry"));
      return(0);
    }
    media->tracks            = floppy_geom.track;
    media->heads             = floppy_geom.head;
    media->sectors_per_track = floppy_geom.sect;
    media->sectors           = floppy_geom.size;
#elif defined(WIN32)
    media->tracks            = tracks;
    media->heads             = heads;
    media->sectors_per_track = spt;
    media->sectors = media->heads * media->tracks * media->sectors_per_track;
#else
    media->tracks            = floppy_type[idx].trk;
    media->heads             = floppy_type[idx].hd;
    media->sectors_per_track = floppy_type[idx].spt;
    media->sectors           = floppy_type[idx].sectors;
#endif
    return(1); // success
    }
  else {
    // unknown file type
    BX_INFO(("unknown mode type"));
    return(0);
    }
}


void
bx_floppy_ctrl_c::enter_result_phase(void)
{

  Bit8u drive;

  drive = BX_FD_THIS s.DOR & 0x03;

  /* these are always the same */
  BX_FD_THIS s.result_index = 0;
  BX_FD_THIS s.main_status_reg = FD_MS_MRQ | FD_MS_DIO | FD_MS_BUSY;

  /* invalid command */
  if ((BX_FD_THIS s.status_reg0 & 0xc0) == 0x80) {
    BX_FD_THIS s.result_size = 1;
    BX_FD_THIS s.result[0] = BX_FD_THIS s.status_reg0;
    return;
  } 

  switch (BX_FD_THIS s.pending_command) {
  case 0x04: // get status
    BX_FD_THIS s.result_size = 1;
    BX_FD_THIS s.result[0] = BX_FD_THIS s.status_reg3;
    break;
  case 0x08: // sense interrupt
    BX_FD_THIS s.result_size = 2;
    BX_FD_THIS s.result[0] = BX_FD_THIS s.status_reg0;
    BX_FD_THIS s.result[1] = BX_FD_THIS s.cylinder[drive];
    break;
  case 0x4a: // read ID
  case 0x4d: // format track
  case 0x46: // read normal data
  case 0x66:
  case 0xc6:
  case 0xe6:
  case 0x45: // write normal data
  case 0xc5:
    BX_FD_THIS s.result_size = 7;
    BX_FD_THIS s.result[0] = BX_FD_THIS s.status_reg0;    
    BX_FD_THIS s.result[1] = BX_FD_THIS s.status_reg1;
    BX_FD_THIS s.result[2] = BX_FD_THIS s.status_reg2;
    BX_FD_THIS s.result[3] = BX_FD_THIS s.cylinder[drive];
    BX_FD_THIS s.result[4] = BX_FD_THIS s.head[drive];
    BX_FD_THIS s.result[5] = BX_FD_THIS s.sector[drive];
    BX_FD_THIS s.result[6] = 2; /* sector size code */
    raise_interrupt();
    break;
  }
}

void
bx_floppy_ctrl_c::enter_idle_phase(void)
{
  BX_FD_THIS s.main_status_reg &= 0x0f;      // leave drive status untouched
  BX_FD_THIS s.main_status_reg |= FD_MS_MRQ; // data register ready

  BX_FD_THIS s.command_complete = 1; /* waiting for new command */
  BX_FD_THIS s.command_index = 0;
  BX_FD_THIS s.command_size = 0;
  BX_FD_THIS s.pending_command = 0;

  BX_FD_THIS s.floppy_buffer_index = 0;
}

