# `himaxprog`

Work in progress tool to program the Himax WiseEye 2 (WE2) series chips through the FT4222H present on the official devkit.

Usage:
```
himaxprog flash detect <arg1> - scan the presence of a flash chip on each SPI bus
himaxprog gpio read - Read from all 4 GPIO pins
himaxprog gpio write <arg1> - Write to selected GPIO pins
himaxprog gpio suspend <arg1> - Write to the 'suspend' GPIO pin
himaxprog gpio wakeup <arg1> - Write to the 'wakeup' GPIO pin
himaxprog reset - Reset the FTDI adapter chip
```

Tor now, it is not able to program anything, as I still need to figure out how to access the flash.

This can still be useful as a debug tool for the FT4222 in this state, as it maps most I/O operations to command line tools.

This uses the FTDI library for the FT4222 chip, as there is no support for it in libftdi1 AFAIK:

<https://ftdichip.com/wp-content/uploads/2024/03/AN_329_User_Guide_for_LibFT4222-v1.8.pdf>
