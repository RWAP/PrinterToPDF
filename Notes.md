<b>Printing International Characters</b>

Epson printers do not support printing 8 bit character codes by default (the printers used to have DIP switches to allow this).  8 Bit character codes are required to support character codes greater than 127 - where the international characters appear.

There is now a new setting ``use8bitchars`` which allows you to set 8 bit character codes by default.  If you enable this, then ensure ``useItalicsCharSet`` is set to 0.

Also check which font you wish to use - some predefined Epson compatible fonts have been provided, as well as a routine to convert Atari ST (GEM) fonts to Epson format.


<b>Speed</b>

The speed of conversion is fairly slow.  We are open to ideas on how to improve this (suggested code changes are even better).

Also ensure that ``imageMode`` is set to 1 (create PNG in memory, as writing to the SD card is slow!)


<b>Page Formats</b>

The code currently supports 5 output formats - alter this by the setting of ``pageSize`` at the start of the initialize() routine.

0 - A4 Portrait with standard Epson Margins (3mm at sides, 8.5mm top, 13.5mm bottom)
1 - A4 Lanscape with standard Epson Margins (3mm at top and bottom, 8.5mm right, 13.5mm left)
2 - 12" Portrait with standard Epson Margins (2.5mm at sides, 10mm at top and bottom)
3 - A4 Portrait with wide Margins (20mm at left, 10mm at right, 10mm at top and bottom)
4 - A4 Lanscape with wide Margins (20mm at top, 10mm at bottom, 10mm at sides)



RWAP Software
August 2020
