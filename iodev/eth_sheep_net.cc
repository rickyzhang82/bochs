/////////////////////////////////////////////////////////////////////////
// $Id: eth_sheep_net.cc,v 1.14 2003/02/16 19:35:57 vruppert Exp $
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

// Peter Grehan (grehan@iprg.nokia.com) coded all of this
// NE2000/ether stuff.

// eth_sheep_net.cc - A Linux socket filter adaptation of the FreeBSD BPF driver
// <splite@purdue.edu> 21 June 2001
//
// Problems and limitations:
//   - packets cannot be sent from BOCHS to the host
//   - Linux kernel sometimes gets network watchdog timeouts under emulation
//   - author doesn't know C++
//
//   The config line in .bochsrc should look something like:
//
//  ne2k: ioaddr=0x280, irq=10, mac=00:a:b:c:1:2, ethmod=linux, ethdev=eth0
//

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include "bochs.h"
#if BX_NE2K_SUPPORT && defined (ETH_LINUX)
#define LOG_THIS bx_devices.pluginNE2kDevice->

extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netpacket/packet.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <linux/types.h>
};

#define BX_PACKET_POLL  1000    // Poll for a frame every 1000 usecs

#define BX_PACKET_BUFSIZ 2048	// Enough for an ether frame
//
//  Define the class. This is private to this module
//
class bx_sn_pktmover_c : public eth_pktmover_c {
public:
  bx_sn_pktmover_c(const char *netif,
         const char *macaddr,
         eth_rx_handler_t rxh,
         void *rxarg);
  void sendpkt(void *buf, unsigned io_len);

private:
  unsigned char *linux_macaddr[6];
  int fd; // /dev/sheep_net file handle
  static void rx_timer_handler(void *);
  void rx_timer(void);
  int rx_timer_index;
};


//
//  Define the static class that registers the derived pktmover class,
// and allocates one on request.
//
class bx_sn_locator_c : public eth_locator_c {
public:
  bx_sn_locator_c(void) : eth_locator_c("linux") {}
protected:
  eth_pktmover_c *allocate(const char *netif,
         const char *macaddr,
         eth_rx_handler_t rxh,
         void *rxarg) {
    return (new bx_sn_pktmover_c(netif, macaddr, rxh, rxarg));
  }
} bx_sn_match;


//
// Define the methods for the bx_linux_pktmover derived class
//

// the constructor
//
bx_sn_pktmover_c::bx_sn_pktmover_c(const char *netif,
               const char *macaddr,
               eth_rx_handler_t rxh,
               void *rxarg)
{
  // clone MAC address
  memcpy(this->linux_macaddr, macaddr, 6);
  const char* dev_name = "/dev/sheep_net";
  // open sheep_net device
  this->fd = open(dev_name, O_RDWR);
  if (this->fd <0) {
    BX_PANIC(("eth_sheep_net: Failed to open /dev/sheep_net: %s", strerror(errno)));
    this->fd = -1;
    return;
  }

  // attach nic to /dev/sheep_net
  if(ioctl(this->fd, SIOCSIFLINK, netif) < 0) {
    BX_PANIC(("eth_sheep_net: Failed to attach %s to sheep_net: %s", netif, strerror(errno)));
    this->fd = -1;
    return;
  }

  int val = fcntl(fd, F_GETFL, 0);
  if (val < 0 || fcntl(this->fd, F_SETFL, val | O_NONBLOCK) <0) {
    BX_PANIC(("eth_sheep_net: could not set non-blocking i/o."));
    close(this->fd);
    this->fd = -1;
    return;
  }

  if(ioctl(this->fd, SIOCSIFADDR, this->linux_macaddr) < 0) {
	 BX_PANIC(("eth_sheep_net: Failed to set MAC address %s to sheep_net: %s", this->linux_macaddr, strerror(errno)));
	 this->fd = -1;
	 return;
  }

  this->rx_timer_index =
    bx_pc_system.register_timer(this, this->rx_timer_handler, BX_PACKET_POLL,
        1, 1, "eth_sheep_net"); // continuous, active

  this->rxh   = rxh;
  this->rxarg = rxarg;
  BX_INFO(("eth_sheep_net: enabled NE2K emulation on interface %s", netif));
}

// the output routine - called with pre-formatted ethernet frame.
void
bx_sn_pktmover_c::sendpkt(void *buf, unsigned io_len)
{
  // ethernet frame header size is 18
  if (this->fd == -1 || io_len < 18)
    return;
  Bit8u* sdbuf = (Bit8u*) buf;
  BX_INFO(("eth_sheep_net: writing to sheep_net %d bytes, dst=%x:%x:%x:%x:%x:%x, src=%x:%x:%x:%x:%x:%x",
           io_len, sdbuf[0], sdbuf[1], sdbuf[2], sdbuf[3], sdbuf[4], sdbuf[5], sdbuf[6], sdbuf[7], sdbuf[8], sdbuf[9], sdbuf[10], sdbuf[11]));
  if(write(this->fd, buf, io_len) < 0)
    BX_INFO(("eth_sheep_net: write failed: %s", strerror(errno)));
}

// The receive poll process
void
bx_sn_pktmover_c::rx_timer_handler(void *this_ptr)
{
  bx_sn_pktmover_c *class_ptr = (bx_sn_pktmover_c *) this_ptr;

  class_ptr->rx_timer();
}

void
bx_sn_pktmover_c::rx_timer(void)
{
  if (this->fd == -1)
    return;

  int nbytes = 0;
  Bit8u rxbuf[BX_PACKET_BUFSIZ];
  nbytes = read(this->fd, rxbuf, 1514);

  if (nbytes == -1) {
    if (errno != EAGAIN)
      BX_INFO(("eth_sheep_net: error receiving packet: %s\n", strerror(errno)));
    return;
  }

  // let through broadcast, multicast, and our mac address
  BX_INFO(("eth_sheep_net: got packet: %d bytes, dst=%x:%x:%x:%x:%x:%x, src=%x:%x:%x:%x:%x:%x", nbytes, rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4], rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8], rxbuf[9], rxbuf[10], rxbuf[11]));
  (*rxh)(rxarg, rxbuf, nbytes);
}
#endif /* if BX_NE2K_SUPPORT && defined ETH_LINUX */
