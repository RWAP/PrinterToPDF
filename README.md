# PrinterToPDF
Project for converting captured dot matrix printer data files to PDF format

Initially this conversion software is taken from the beta conversion application we are creating for the Retro-Printer module (http://www.retroprinter.com).  That project is (c) RWAP Software - www.rwapsoftware.co.uk

Whilst the module for capturing the printer data itself is not provided, this code is provided under the GNU General Public License, as it is felt that it would be of interest and benefit for those wanting to create software for converting Epson (ESC/P and ESC/P2) dot matrix and centronics inkjet printer data files to PDF format.

It is hoped that support for PCL (and potentially ESC/POS) could also be added and this software would then provide a modern replacement for the old epsonps software - http://www.retroarchive.org/garbo/pc/source/epsonps.zip.

At present, the code has basic ESC/P conversion functionality and will need some re-writing of the code to form a stand-alone module.  In particular, you will need to adapt the function read_byte_from_printer() to open any input file (or stream) and fetch the next byte to be processed.

The PrinterConvert code works by creating a bitmap image in memory which is then built up as it interprets the captured printer data.  Once the bitmap image for a page is completely rendered, then that is converted to a PDF (ready for printing if you wish). The software also supports the SDL code to output a copy of the page being built to the screen should you want this (look for sdlon in the code).

RWAP Software
March 2017

COMPILING
On some versions of Linux, you will need to change the reference in line 6 to read:

<code>#include "/usr/include/SDL/SDL.h"</code>

To compile the program use the following command:

<code>gcc PrinterConvert.c \`sdl-config --cflags --libs\` -o PrinterConvert -lrt  -g</code>
