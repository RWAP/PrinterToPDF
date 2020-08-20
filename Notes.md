<b>Printing International Characters</b>

Epson printers do not support printing 8 bit character codes by default (the printers used to have DIP switches to allow this).  8 Bit character codes are required to support character codes greater than 127 - where the international characters appear.

There is now a new setting ``use8bitchars`` which allows you to set 8 bit character codes by default.  If you enable this, then ensure ``useItalicsCharSet`` is set to 0.

This can also be set using -8 as a parameter (for 8 bit character set), or -i (for italics)

Also check which font you wish to use - some predefined Epson compatible fonts have been provided, as well as a routine to convert Atari ST (GEM) fonts to Epson format.


<b>Speed</b>

The speed of conversion is fairly slow.  We are open to ideas on how to improve this (suggested code changes are even better).

Also ensure that ``imageMode`` is set to 1 (create PNG in memory, as writing to the SD card is slow!)


RWAP Software
August 2020
