# `himaxprog`

Work in progress tool to program the Himax WiseEye 2 (WE2) series chips through the FT4222H present on the official devkit.

## Build

```
# Download and extract the libftdi in /usr/local, in example here for Linux
mkdir -p /tmp/libftdi
curl -o libft4222.tar.gz https://ftdichip.com/wp-content/uploads/2024/03/libft4222-linux-1.4.4.188.tgz
tar -xz -C /tmp/libftdi -f libft4222.tar.gz

# Install the libftdi
sudo sh /tmp/libftdi/install4222.sh

# Install himaxprog in /usr/local
make install
```

## Usage

```
himaxprog flash detect <arg1>
    scan the presence of a flash chip on each SPI bus
himaxprog spi <arg1> <arg2>
    Write hex data <arg1> over SPI, then read <arg2> number of bytes
himaxprog gpio read
    Read from all 4 GPIO pins
himaxprog gpio write <arg1>
    Write to selected GPIO pins
himaxprog gpio suspend <arg1>
    Write to the 'suspend' GPIO pin
himaxprog gpio wakeup <arg1>
    Write to the 'wakeup' GPIO pin
himaxprog reset
    Reset the FTDI adapter chip
```

Tor now, it is not able to program anything, as I still need to figure out how to access the flash.

This can still be useful as a debug tool for the FT4222 in this state, as it maps most I/O operations to command line tools.

This uses the FTDI library for the FT4222 chip, as there is no support for it in libftdi1 AFAIK:

<https://ftdichip.com/wp-content/uploads/2024/03/AN_329_User_Guide_for_LibFT4222-v1.8.pdf>
