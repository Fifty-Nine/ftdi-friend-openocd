## OpenOCD support for the Adafruit FTDI Friend ##

This repository implements OpenOCD support for the [Adafruit FTDI Friend](https://www.adafruit.com/product/284)
as a JTAG programmer.

![FTDI Friend programming a Microchip Explorer16 dev board](https://raw.githubusercontent.com/Fifty-Nine/openocd/gh-pages/images/dev-board.jpg)

### Table of Contents ###

* [Quickstart Guide](#quickstart-guide)
* [Background](#background)
* [SRST and TRST support](#srst-and-trst-support)
* [Acknowledgements](#acknowledgements)

### Quickstart Guide ###

This driver has been tested and confirmed to work on Ubuntu 16.04.2 LTS as
well as Debian 8.5. Other linux platforms should work, but Windows support
likely requires patches to the driver source code.

#### Build libftdi ####

First ensure that you have all the necessary tools required to build vanilla
OpenOCD and libftdi. The easiest way to do this is to use the `apt-get build-dep`
command:

```
$ sudo apt-get build-dep openocd libftdi1
```

The driver requires a recent version of libftdi with support for
asynchronous reads and writes. Unfortunately, the version in the
Ubuntu repositories isn't recent enough, so we'll need to build
and install that separately:

```
$ git clone git://developer.intra2net.com/libftdi
$ mkdir -p libftdi/build
$ cd libftdi/build
$ cmake ..
$ make
$ sudo make install
```

#### Build the patched OpenOCD ####

Next, clone the repository:

```
$ git clone https://github.com/Fifty-Nine/openocd.git
```

Configure, build and install the source:

```
$ mkdir -p openocd/build
$ cd openocd/build
$ ../configure --enable-ftdi-friend
$ make
$ sudo make install
```

We've installed some new shared libraries, so be sure to run ldconfig:

```
$ sudo ldconfig
```

#### Connect the target board ####

Now we're ready to use our custom-built OpenOCD, but before we can start we
need to wire the FTDI Friend to our target. This will of course depend on your
target, but the FTDI Friend pinout is fixed:

| FTDI Friend Pin   | JTAG Pin |
| ----------------- | -------- |
| RTS               | TDI      |
| RX                | TCK      |
| TX                | TDO      |
| VCC<sup>[1]</sup> | NC       |
| CTS               | TMS      |
| GND               | GND      |
| DTR<sup>[2]</sup> | SRST     |
| DSR<sup>[2]</sup> | TRST     |

[1] The FTDI friend could be used to power your target board, but this will depend
on your power requirements. I haven't experimented with this.<br/>
[2] By default, the DTR and DSR pins are unconnected on the FTDI friend board. Using
them requires some creativity, but they typically aren't needed anyway. See the
[SRST and TRST](#srst-and-trst-support) section below for details.

#### Connect the FTDI Friend ####

We need to connect the FTDI Friend to the host machine, but by default the device
will require root permissions to access. To fix this, we can create a udev rule
to set the appropriate permissions on the USB device.

```
$ echo 'ATTRS{idVendor}=="0403",ATTRS{idProduct}=="6001",MODE="0666",GROUP="users"' > ftdi-friend.rules
$ sudo mv ftdi-friend.rules /etc/udev/rules.d
```

#### Run OpenOCD ####

With everything connected, we can start up OpenOCD. We can create a custom `.cfg` file,
or just tell OpenOCD what to use from the command line. Here's an example of connecting
to the Microchip Explorer16 development board:

```
$ openocd -c "interface ftdi_friend" -c "adapter_khz 100000" -f "board/microchip_explorer16.cfg" -c "interface ftdi_friend"
Open On-Chip Debugger 0.10.0+dev-00097-g272b05d-dirty (2017-03-21-22:31)
Licensed under GNU GPL v2
For bug reports, read
        http://openocd.org/doc/doxygen/bugs.html
Info : only one transport option; autoselect 'jtag'
adapter speed: 100000 kHz
adapter_nsrst_delay: 100
jtag_ntrst_delay: 100
Warn : Interface already configured, ignoring
Info : clock speed 100000 kHz
Info : JTAG tap: pic32mx.cpu tap/device found: 0x30938053 (mfg: 0x029 (MicrochipTechnology), part: 0x0938, ver: 0x3)
```

And there you have it! JTAG via the FTDI Friend. Now we can do whatever we would normally do with a
JTAG programmer:

```
$  telnet localhost 4444
> halt
target halted in MIPS32 mode due to debug-request, pc: 0x9d000294
> reg status
status (/32): 0x00100001
>
```

and so on.

### SRST and TRST support ###

The FTDI friend header provides direct access to power, ground and four data lines. However,
complete JTAG support requires six: TMS, TDI, TDO, TCK, SRST and TRST. As a result, without
modification, the FTDI friend driver only supports targets that do not require an SRST line
(note that TRST is never required since the TAP state machine can be reset through other means).
Generally, even if targets *have* an SRST pin, it doesn't need to be connected. That said,
it's possible to modify the FTDI friend with a little soldering to add a SRST pin.

The FTDI Friend provides several pads on the reverse side that allow configuring the various
aspects like line voltage and (importantly!) whether pin 6 is connected to DTR or RTS. By
default, pin 6 is shorted to RTS. We can take advantage of these solder pads to get access to
the FT232RL's DTR line. We just need to solder a wire onto the pad connected to DTR. Here's
a picture of what that looks like:

![FTDI friend with DTR wire](https://raw.githubusercontent.com/Fifty-Nine/openocd/gh-pages/images/ftdi-srst.jpg)

Doing the same for TRST is not possible, since it is not connected to the FTDI friend board at
all. It's probably not worth trying. If you really need TRST for some reason, you could recompile
the driver to use a different pinout, but you will inevitably lose access to some other pin.
Alternatively, you could try using a grabber of some sort, like the [Pomona 72902 series](http://www.digikey.com/product-detail/en/pomona-electronics/72902-0/501-1172-ND/1196307).
Finally, if you've got some fine-gauge wire and excellent soldering skills, you might try soldering
directly to the 0.5mm pitch pins of the FT232RL. Good luck!

### Background ###

While working on a reverse engineering project, I found myself in need of
a JTAG programmer. After doing some research, I discovered that OpenOCD has
support for various FTDI-based USB-to-serial converters. Unfortunately, the one
I had--the Adafruit FTDI Friend--uses an FT232RL chip, which isn't supported
by the available OpenOCD drivers.

I found an example of using the FT232RL as a JTAG programmer [here](http://vak.ru/doku.php/proj/bitbang/bitbang-jtag).
Unfortunately, I wasn't able to get that patch working with a recent OpenOCD version.
It's possible that my wiring was wrong, or that I had incorrectly resolved the patch
conflicts, but the relatively small size of the patch encouraged me to develop my own
solution. Much credit is owed to the author of that work since without it I likely
wouldn't have even tried.

### Acknowledgements ###

This work would not have been possible without the contributions of several people and organizations.
Thanks to Serge Vakulenko for authoring the original FT232RL OpenOCD driver that inspired this
one--without that driver as a proof of concept, I'd have never written this one. Likewise, thanks
to Adafruit for providing excellent documentation of the FTDI friend, especially the schematics.
Finally, shout out to @benjholla for just bein' a good dude.


