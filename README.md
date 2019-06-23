# PrinterToPDF
Project for converting captured Epson ESC/P and ESC/P2 dot matrix and inkjet printer data files to PDF format

Initially this conversion software is taken from the epson convertor which we created for the hardware Retro-Printer Module which captures centronics printer data and converts to e-documents, or prints to modern printers (http://www.retroprinter.com).  That project is (c) RWAP Software - www.rwapsoftware.co.uk

This code is provided under the GNU General Public License, as it is felt that it would be of interest and benefit for those wanting to create software for converting Epson (ESC/P and ESC/P2) dot matrix and centronics inkjet printer data files to PDF format.

It is hoped that support for ESC/POS could also be added and this software would then provide a modern replacement for the old epsonps software - http://www.retroarchive.org/garbo/pc/source/epsonps.zip.

At present, the code will take a file called 1.prn in Epson ESC/P or ESC/P2 format and convert it to a PDF

The PrinterConvert code works by creating a bitmap image in memory which is then built up as it interprets the captured printer data.  Once the bitmap image for a page is completely rendered, then that is converted to a PDF (ready for printing if you wish). The software also supports the SDL code to output a copy of the page being built to the screen should you want this (look for sdlon in the code).

RWAP Software
January 2019

PRE-REQUISITES
You will need libpng, ImageMagick, SDL libHARU installed on Linux. 

For Debian (on the Raspberry Pi), this means running;
<code>apt-get install libpng ImageMagick</code>

sudo apt-get install libsdl1.2-dev
sudo apt-get install libsdl-image1.2-dev

LibHaru has to be downloaded from source and compiled:
http://libharu.org/
- detailed installation instructions appear at: https://github.com/libharu/libharu/wiki/Installation
You may also be able to use : <code>sudo apt install libhpdf-2.2.1 libhpdf-dev</code> although the latest version is v2.3.0.


Ensure you copy BOTH dir.c and PrinterConvert.c to your working directory

COMPILING
On some versions of Linux, you will need to change the reference in line 8 to read:
<code>#include "/usr/local/include/hpdf.h"</code>

and on line 9 to read:
<code>#include "/usr/local/include/SDL/SDL.h"</code>


To compile the program use the following command:

<code>gcc PrinterConvert.c `sdl-config --cflags --libs` -o printerToPDF -lrt -lhpdf -lpng</code>


FOr testing, store the file (called test1.prn) to be converted in the same directory as printerToPDF (eg /home/printerToPDF)

You can then convert it with:
./printerToPDF 3 0 font2/SIEMENS.C16 1 sdloff /home/printerToPDF/output

You could easily change the code to pass the input filename as a parameter.


DISCUSSION
For general discussion about the PrinterToPDF software and the Retro-Printer project which forms the basis for this conversion software, please visit the forums - http://forum.retroprinter.com
