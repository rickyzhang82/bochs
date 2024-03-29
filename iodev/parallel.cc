/////////////////////////////////////////////////////////////////////////
// $Id: parallel.cc,v 1.24 2003/10/29 17:29:26 vruppert Exp $
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
////////////////////////////////////////////////////////
// This code was just a few stubs until Volker.Ruppert@t-online.de 
// fixed it up in November 2001.


// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE 
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include "bochs.h"
#define LOG_THIS theParallelDevice->

bx_parallel_c *theParallelDevice = NULL;

  int
libparallel_LTX_plugin_init(plugin_t *plugin, plugintype_t type, int argc, char *argv[])
{
  theParallelDevice = new bx_parallel_c ();
  bx_devices.pluginParallelDevice = theParallelDevice;
  BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theParallelDevice, BX_PLUGIN_PARALLEL);
  return(0); // Success
}

  void
libparallel_LTX_plugin_fini(void)
{
}

bx_parallel_c::bx_parallel_c(void)
{
  put("PAR");
  settype(PARLOG);
  s.output = NULL;
}

bx_parallel_c::~bx_parallel_c(void)
{
  if (s.output != NULL)
    fclose(s.output);
}

  void
bx_parallel_c::init(void)
{
  BX_DEBUG(("Init $Id: parallel.cc,v 1.24 2003/10/29 17:29:26 vruppert Exp $"));

  if (bx_options.par[0].Oenabled->get ()) {

    /* PARALLEL PORT 1 */

    DEV_register_irq(7, "Parallel Port 1");
    BX_INFO (("parallel port 1 at 0x378 irq 7"));
    for (unsigned addr=0x0378; addr<=0x037A; addr++) {
      DEV_register_ioread_handler(this, read_handler, addr, "Parallel Port 1", 1);
      }
    DEV_register_iowrite_handler(this, write_handler, 0x0378, "Parallel Port 1", 1);
    DEV_register_iowrite_handler(this, write_handler, 0x037A, "Parallel Port 1", 1);

    BX_PAR_THIS s.STATUS.error = 1;
    BX_PAR_THIS s.STATUS.slct  = 1;
    BX_PAR_THIS s.STATUS.pe    = 0;
    BX_PAR_THIS s.STATUS.ack   = 1;
    BX_PAR_THIS s.STATUS.busy  = 1;

    BX_PAR_THIS s.CONTROL.strobe   = 0;
    BX_PAR_THIS s.CONTROL.autofeed = 0;
    BX_PAR_THIS s.CONTROL.init     = 1;
    BX_PAR_THIS s.CONTROL.slct_in  = 1;
    BX_PAR_THIS s.CONTROL.irq      = 0;
    BX_PAR_THIS s.CONTROL.input    = 0;

    BX_PAR_THIS s.initmode = 0;

    if (strlen(bx_options.par[0].Ooutfile->getptr ()) > 0) {
      s.output = fopen(bx_options.par[0].Ooutfile->getptr (), "wb");
      if (!s.output)
        BX_PANIC (("Could not open '%s' to write parport1 output",
                   bx_options.par[0].Ooutfile->getptr ()));
    }
  }
}

  void
bx_parallel_c::reset(unsigned type)
{
}

  void
bx_parallel_c::virtual_printer(void)
{
  if (BX_PAR_THIS s.STATUS.slct) {
    if (BX_PAR_THIS s.output != NULL) {
      fputc(BX_PAR_THIS s.data, BX_PAR_THIS s.output);
      fflush (BX_PAR_THIS s.output);
      }
    if (BX_PAR_THIS s.CONTROL.irq == 1) {
      DEV_pic_raise_irq(7);
      }
    BX_PAR_THIS s.STATUS.ack = 0;
    BX_PAR_THIS s.STATUS.busy = 1;
    }
  else {
    BX_ERROR(("data is valid, but printer is offline"));
    }
}

  // static IO port read callback handler
  // redirects to non-static class handler to avoid virtual functions

  Bit32u
bx_parallel_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
#if !BX_USE_PAR_SMF
  bx_parallel_c *class_ptr = (bx_parallel_c *) this_ptr;

  return( class_ptr->read(address, io_len) );
}


  Bit32u
bx_parallel_c::read(Bit32u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif  // !BX_USE_PAR_SMF

  Bit32u retval;

  switch (address) {
    /* PARALLEL PORT 1 */
    case 0x0378:
      if (!BX_PAR_THIS s.CONTROL.input) {
        return (Bit32u)BX_PAR_THIS s.data;
      } else {
        BX_ERROR(("read: input mode not supported"));
        return (0xFF);
      }
      break;
    case 0x0379:
      {
        retval = ((BX_PAR_THIS s.STATUS.busy  << 7) |
                  (BX_PAR_THIS s.STATUS.ack   << 6) |
                  (BX_PAR_THIS s.STATUS.pe    << 5) |
                  (BX_PAR_THIS s.STATUS.slct  << 4) |
                  (BX_PAR_THIS s.STATUS.error << 3));
        if (BX_PAR_THIS s.STATUS.ack == 0) {
          BX_PAR_THIS s.STATUS.ack = 1;
          if (BX_PAR_THIS s.CONTROL.irq == 1) {
            DEV_pic_lower_irq(7);
          }
        }
        if (BX_PAR_THIS s.initmode == 1) {
          BX_PAR_THIS s.STATUS.busy  = 1;
          BX_PAR_THIS s.STATUS.slct  = 1;
          BX_PAR_THIS s.STATUS.ack  = 0;
          if (BX_PAR_THIS s.CONTROL.irq == 1) {
            DEV_pic_raise_irq(7);
          }
          BX_PAR_THIS s.initmode = 0;
        }
        BX_DEBUG(("read: status register returns 0x%02x", retval));
        return retval;
      }
      break;
    case 0x037A:
      {
        retval = ((BX_PAR_THIS s.CONTROL.input    << 5) |
                  (BX_PAR_THIS s.CONTROL.irq      << 4) |
                  (BX_PAR_THIS s.CONTROL.slct_in  << 3) |
                  (BX_PAR_THIS s.CONTROL.init     << 2) |
                  (BX_PAR_THIS s.CONTROL.autofeed << 1) |
                  (BX_PAR_THIS s.CONTROL.strobe));
        BX_DEBUG(("read: control register returns 0x%02x", retval));
        return retval;
      }
      break;
  }
  return(0);
}


  // static IO port write callback handler
  // redirects to non-static class handler to avoid virtual functions

  void
bx_parallel_c::write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
#if !BX_USE_PAR_SMF
  bx_parallel_c *class_ptr = (bx_parallel_c *) this_ptr;

  class_ptr->write(address, value, io_len);
}

  void
bx_parallel_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif  // !BX_USE_PAR_SMF

  switch (address) {
    /* PARALLEL PORT 1 */
    case 0x0378:
      BX_PAR_THIS s.data = (Bit8u)value;
      BX_DEBUG(("write: data output register = 0x%02x", (Bit8u)value));
      break;
    case 0x037A:
      {
        if ((value & 0x01) == 0x01) {
          if (BX_PAR_THIS s.CONTROL.strobe == 0) {
            BX_PAR_THIS s.CONTROL.strobe = 1;
            virtual_printer(); // data is valid now
          }
        } else {
          if (BX_PAR_THIS s.CONTROL.strobe == 1) {
            BX_PAR_THIS s.CONTROL.strobe = 0;
          }
        }
        BX_PAR_THIS s.CONTROL.autofeed = ((value & 0x02) == 0x02);
        if ((value & 0x04) == 0x04) {
          if (BX_PAR_THIS s.CONTROL.init == 0) {
            BX_PAR_THIS s.CONTROL.init = 1;
            BX_PAR_THIS s.STATUS.busy  = 0;
            BX_PAR_THIS s.STATUS.slct  = 0;
            BX_PAR_THIS s.initmode = 1;
            BX_DEBUG(("printer init requested"));
          }
        } else {
          if (BX_PAR_THIS s.CONTROL.init == 1) {
            BX_PAR_THIS s.CONTROL.init = 0;
          }
        }
        if ((value & 0x08) == 0x08) {
          if (BX_PAR_THIS s.CONTROL.slct_in == 0) {
            BX_PAR_THIS s.CONTROL.slct_in = 1;
            BX_DEBUG(("printer now online"));
          }
        } else {
          if (BX_PAR_THIS s.CONTROL.slct_in == 1) {
            BX_PAR_THIS s.CONTROL.slct_in = 0;
            BX_DEBUG(("printer now offline"));
          }
        }
        BX_PAR_THIS s.STATUS.slct = BX_PAR_THIS s.CONTROL.slct_in;
        if ((value & 0x10) == 0x10) {
          if (BX_PAR_THIS s.CONTROL.irq == 0) {
            BX_PAR_THIS s.CONTROL.irq = 1;
            BX_DEBUG(("irq mode selected"));
          }
        } else {
          if (BX_PAR_THIS s.CONTROL.irq == 1) {
            BX_PAR_THIS s.CONTROL.irq = 0;
            BX_DEBUG(("polling mode selected"));
          }
        }
        if ((value & 0x20) == 0x20) {
          if (BX_PAR_THIS s.CONTROL.input == 0) {
            BX_PAR_THIS s.CONTROL.input = 1;
            BX_DEBUG(("data input mode selected"));
          }
        } else {
          if (BX_PAR_THIS s.CONTROL.input == 1) {
            BX_PAR_THIS s.CONTROL.input = 0;
            BX_DEBUG(("data output mode selected"));
          }
        }
        if ((value & 0xC0) > 0) {
          BX_ERROR(("write: unsupported control bit ignored"));
        }
      }
      break;
  }
}
