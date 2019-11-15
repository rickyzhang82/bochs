Forked Bochs 2.11 for Minix 2.0.0
=================================

[![Build Status](https://api.cirrus-ci.com/github/rickyzhang82/bochs.svg)](https://cirrus-ci.com/github/rickyzhang82/bochs.svg)

Bochs 2.11 is the last known version of Bochs to me that I could run [Minix 2.0.0](http://download.minix3.org/previous-versions/Intel-2.0.0/) with TCP/IP networking. In my junior college year, I read Andrew Tanebaum's book `Operating Systems: Design and Implementation (Second Edition)` and did some OS lab work in Minix 2.0.0.

However, the recent version of Bochs discontinues support for level 3 Intel CPU a.k.a i386. Thus, you can not run Minix 2.0.0 in Bochs any more. For sure, you can run Minix in the latest QEMU or VirtualBox. But it has no networking due to the absence of N2k ethernet card emulation. So I forked Bochs 2.11 for those who still want to have fun with Minix 2.0.0.


Why Minix 2.0.0?
----------------

Minix has changed a lot since the 2000s. Minix 3.x can even run in an embedded device like BeagleBone!

But if you want something simple, Minix 2.0.0 is still a good choice. With merely `92,244` lines of C code and `10,311` lines of C header, you get a clean-design **\*nix** microkernel operating system that supports MMU, multi-process, and TCP/IP networking. The total lines of code in version 2.0.0 is still under a manageable level that one single person can grasp the concept and implementation quickly.

Long live Minix 2.0.0!


Build Bochs
-----------

The repository only supports Linux. I tested it in 64 bit Fedora 31.

```
git clone https://github.com/rickyzhang82/bochs.git

make clean

./configure --with-x --with-X11 --enable-all-optimizations --enable-idle-hack --enable-readline --enable-plugins --enable-cpu-level=3 --enable-fpu --enable-fast-function-calls --enable-cpp --enable-iodebug --enable-x86-debugger --enable-ne2000 --disable-mmx

make -j 16

sudo make install
```


Network Support
---------------

Compile `sheep_net` module from Basilisk II emulator. You can read [the technical document of sheep_net module I wrote here](https://github.com/rickyzhang82/macemu/tree/master/BasiliskII/src/Unix/Linux/NetDriver).

```
git clone https://github.com/rickyzhang82/macemu.git

cd macemu/BasiliskII/src/Unix/Linux/NetDriver

make
//create sheep_net device node
sudo make dev
sudo chown [user account] /dev/sheep_net
sudo make install
sudo modprobe sheep_net

```

I added a new network mode called `sheep_net` in Bochs 2.11. You can access Minix telnet and ftp service from outside.




Install Minix 2.0.0
--------------------


### Create boot floppy image

Download [Intel-2.0.0.tar.bz2](http://download.minix3.org/previous-versions/bzipped/Intel-2.0.0.tar.bz2).

```
tar xvf Intel-2.0.0.tar.bz2
dd if=/dev/zero of=boot.img bs=1k count=1440
cat Intel-2.0.0/i386/ROOT Intel-2.0.0/i386/USR | dd of=boot.img conv=notrunc
```

You will use this floppy `boot.img` to boot Minix 2.0.0 in Bochs later.

### Create a blank hd image

```
[Ricky@gtx Minix-2.0.0]$ bximage
========================================================================
                                bximage
                  Disk Image Creation Tool for Bochs
        $Id: bximage.c,v 1.19 2003/08/01 01:20:00 cbothamy Exp $
========================================================================

Do you want to create a floppy disk image or a hard disk image?
Please type hd or fd. [hd] 

What kind of image should I create?
Please type flat, sparse or growing. [flat] 

Enter the hard disk size in megabytes, between 1 and 32255
[10] 200

I will create a 'flat' hard disk image with
  cyl=406
  heads=16
  sectors per track=63
  total sectors=409248
  total size=199.83 megabytes

What should I name the image?
[c.img] minix.img

Writing: [] Done.

I wrote 209534976 bytes to minix.img.

The following line should appear in your bochsrc:
  ata0-master: type=disk, path="minix.img", mode=flat, cylinders=406, heads=16, spt=63
```

Copy the last line of hard disk image parameter for Bochs configuration file .



### Install root file system

Boot from `boot.img` floppy.

```
# Bochs 2.1.1
# emulate minix 2.0.0

megs: 16
romimage: file=$BXSHARE/BIOS-bochs-latest, address=0xf0000
vgaromimage: $BXSHARE/VGABIOS-elpin-2.40
ata0-master: type=disk, path="minix.img", mode=flat, cylinders=203, heads=16, spt=63
floppya: 1_44="boot.img", status=inserted
# uncomment the line below if you want to boot from hd
# boot: disk
boot: floppy
log: bochs.log
panic: action=ask
error: action=report
info: action=report
debug: action=ignore
clock: sync=realtime, time0=1
ips: 8000000
mouse: enabled=0
user_shortcut: keys=ctrlaltdel
ne2k: ioaddr=0x300, irq=3, mac=b0:c4:20:00:00:00, ethmod=sheep_net, ethdev=wlo1
```

Follow the instruction in `Intel-2.0.0/misc/example.txt`:

- Create Minix partition and file system
- Install root file system


### Install /usr

The size of `Intel-2.0.0/i386/USR.TAZ` is more than 1.44MB floppy can handle. The trick is to run `dd` command on demand -- in other word, run `dd` to overwrite only when it asks for next floppy.

In Minix terminal, do the following:

```
setup /usr
```

In the host OS, do the following on demand:

```
dd if=USR.TAZ of=boot.img count=1440 bs=1024 skip=0
dd if=USR.TAZ of=boot.img count=1440 bs=1024 skip=1440
dd if=USR.TAZ of=boot.img count=1440 bs=1024 skip=2880
```

You can apply the same trick to install source code `Intel-2.0.0/src/SYS.TAZ` and `Intel-2.0.0/src/CMD.TAZ`.

Don't forget to switch back boot from disk in Bochs configuration file.


### Setup boot environment variables to enable N2k

In Minix terminal, do the following:

```
halt

; command set show all vars
; command save save
; save ethernet
DPETH0 = 300:3

save
```

This took me a while to figure out why N2k card is in sink mode. See [monitor doc here](https://minix1.woodhull.com/current/2.0.4/wwwman/man8/monitor.8.html).


### Patch dp8390 chip driver

```C
--- /home/Ricky/Bochs-workspace/minix-2.0.0/pkg/Intel-2.0.0/src/src/kernel/dp8390.c	1996-10-01 08:00:00.000000000 -0400
+++ src/kernel/dp8390.c	2019-11-14 09:55:50.000000000 -0500
@@ -341,7 +341,7 @@
 		outb_reg0(dep, DP_TPSR, dep->de_sendq[sendq_head].sq_sendpage);
 		outb_reg0(dep, DP_TBCR1, size >> 8);
 		outb_reg0(dep, DP_TBCR0, size & 0xff);
-		outb_reg0(dep, DP_CR, CR_TXP);	/* there it goes.. */
+		outb_reg0(dep, DP_CR, CR_TXP|CR_STA);	/* there it goes.. */
 	}
 	else
 		dep->de_sendq[sendq_head].sq_size= size;
@@ -838,7 +838,7 @@
 					dep->de_sendq[sendq_tail].sq_sendpage);
 				outb_reg0(dep, DP_TBCR1, size >> 8);
 				outb_reg0(dep, DP_TBCR0, size & 0xff);
-				outb_reg0(dep, DP_CR, CR_TXP);	/* there is goes.. */
+				outb_reg0(dep, DP_CR, CR_TXP|CR_STA);	/* there is goes.. */
 			}
 			if (dep->de_flags & DEF_SEND_AVAIL)
 				dp_send(dep);
```
