Printing International Characters.

There are a couple of issues here:
a) Epson printers do not support printing 8 bit character codes by default (the printers used to have DIP switches to allow this).  8 Bit character codes are required to support character codes greater than 127 - where the international characters appear.
b) The supplied free font does not include umlauts etc.  We have created a range of fonts for use with our RetroPrinter project.

See https://github.com/RWAP/PrinterToPDF/issues/22 for more details.


Speed

The speed of conversion is fairly slow.  We are open to ideas on how to improve this (suggested code changes are even better).

Also ensure that imageMode is set to 1 (create PNG in memory, as writing to the SD card is slow!)

RWAP Software
August 2020
