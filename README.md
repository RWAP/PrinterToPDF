# PrinterToPDF
Project for converting captured Epson ESC/P and ESC/P2 dot matrix and inkjet printer data files to PDF format

Initially this conversion software is taken from the epson convertor which we created for the hardware Retro-Printer Module which captures centronics printer data and converts to e-documents, or prints to modern printers (http://www.retroprinter.com).  That project is (c) RWAP Software - www.rwapsoftware.co.uk

This code is provided under the GNU General Public License, as it is felt that it would be of interest and benefit for those wanting to create software for converting Epson (ESC/P and ESC/P2) dot matrix and centronics inkjet printer data files to PDF format.

It is hoped that support for ESC/POS could also be added and this software would then provide a modern replacement for the old epsonps software - http://www.retroarchive.org/garbo/pc/source/epsonps.zip.

At present, the code will take a file called 1.prn in Epson ESC/P or ESC/P2 format and convert it to a PDF

The PrinterConvert code works by creating a bitmap image in memory which is then built up as it interprets the captured printer data.  Once the bitmap image for a page is completely rendered, then that is converted to a PDF (ready for printing if you wish). The software also supports the SDL code to output a copy of the page being built to the screen should you want this (look for sdlon in the code).

RWAP Software
January 2019

<b>Quick start with Docker</b>
Build Docker image.
<code>sudo docker build -t xxx/escp2pdf .</code>
Use Docker image.
<code>sudo docker run -itd --name escp2pdf --restart=always -v /home/anuser/escp/:/opt/ xxx/escp2pdf</code>
<code>sudo docker exec -it escp2pdf printerToPDF -o /opt/output -f font2/Epson-Standard.C16 /opt/test.dat</code>

<b>PRE-REQUISITES</b>
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

<b>COMPILING</b>
On some versions of Linux, you will need to change the reference in line 8 to read:
<code>#include "/usr/local/include/hpdf.h"</code>

and on line 9 to read:
<code>#include "/usr/local/include/SDL/SDL.h"</code>

To compile the program, simply run `make`.

<b>INSTALLATION</b>

`make install` installs the executable to `/usr/bin`, and the font files to `/usr/lib/PrinterToPDF`. You can override the destination by passing a value for `prefix`. For example, to install in `/usr/local/bin` and `/usr/local/lib/PrinterToPDF`, type the following:

`make prefix=/usr/local install`


<b>USAGE</b>
PrinterToPDF allows various parameters to be passed as part of the command line.  These can be passed in any order.

Call it with 
`printerToPDF -o PATH -f FONT [OPTIONS] [FILE]`

Mandatory Options are:
    -o PATH                 A directory to store the output files (eg. /home/pi/data/) 
    -f FONT                 Name of font file to use (eg font2/Epson-Standard.C16)
    
Options:    
    -d                      Set font direction right or left.  Default is -d right
    -s [NUM]                Display printout in sdl window.  The optional numeric argument reduces sdl display to that percentage of original size
    -p NUM                  Select predefined page size:
                            0: A4 portrait (210 x 297 mm) (default)
                            1: A4 landscape (297 x 210 mm)
                            2: 12" paper - US Single Sheet (8 x 12")

                            <b>OR</b>

    -p W,H                  Select custom page size in millimeters (W: width, H: height) - for example, -p 210,297
    -m [L],[R],[T],[B]      Set page margins in millimeters
                            You can leave fields blank to keep the default values:
                            L (left):    3.0 mm (paper size 2:  2.5 mm)
                            R (right):   3.0 mm (paper size 2:  2.5 mm)
                            T (top):     8.5 mm (paper size 2: 10.0 mm)
                            B (bottom): 13.5 mm (paper size 2: 10.0 mm)

                            <b>OR</b>

    -m V,H                  Set page margins in millimeters - V is vertical sides (left and right), H is horizontal sides (top and bottom)

                            <b>OR</b>
                            
    -m A                    Set same margins in millimeters for all sides
    -l STR                  Set linefeed ending:
                            none: No conversion (default)
                            unix:    Unix (LF)
                            windows: Windows (CR+LF)
                            mac:     MAC (CR)
    -8                      Use 8 Bit characters (CANNOT be used with -i)
    -9                      Use 9 Pin printing (ESC/P standard for line feeds)
    -i                      Use character codes 160-255 as italics (CANNOT be used with -8)
    -q                      Quiet mode - do not output any messages
    
    -h                      Display this help and exit
    -v                      Display version information and exit
    
    [FILE]                  This is the raw data file to be converted.  If not specified, input is read from stdin
                            
The default would be:

`printerToPDF -o PATH -f FONT -d right -p 0 -m 3,3,8.5,13.5 -l none`



<b>IMPORTANT NOTE</b>
The ESC/P standard and ESC/P2 standard are different in how they handle line spacing.  The ESC/P standard uses 1/72" units, whereas the later ESC/P2 standard uses 1/60" units.

As a result the line spacing may be incorrect if used with programs which expect a 9 pin (ESC/P) printer.

If the produced output appears elongated (or has apparent extra line feeds), try changing the setting in PrinterConvert.c on line 45 to read:
<code>int needles = 9;</code>


<b>DISCUSSION</b>
For general discussion about the PrinterToPDF software and the Retro-Printer project which forms the basis for this conversion software, please visit the forums - https://www.retroprinter-support.com/viewforum.php?f=10
