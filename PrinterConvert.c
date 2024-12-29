#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <png.h>
#include <setjmp.h>
#include "/usr/include/hpdf.h"
#include "/usr/include/SDL/SDL.h"
#include "dir.c"

const char* version = "v1.11";

/* Conversion program to convert Epson ESC/P printer data to an Adobe PDF file on Linux.
 * v1.11
 *
 * v1.0 First Release - taken from the conversion software currently in development for the Retro-Printer module.
 * v1.1 Swithced to using libHaru library to create the PDF file for speed and potential future enhancements - see http://libharu.org/
 *      Tidied up handling of the default configuration files
 * v1.2 Removed possibility of a stack dump where tries to unlink both imagememory and seedrow
 * v1.3 General code tidy and improve handling of 9 pin printers
 * v1.4 Added Auto-Linefeed configuration, handle extended control codes and bring up to the same standard as v3.2 Retro-Printer Software
 * v1.5 Speed improvements
 * v1.6 Minor changes to instructions and set up INPUT_FILENAME as a definition
 * v1.6.1 Changed usage instructions
 * v1.6.2 Minor bug fix to the use of letter quality
 * v1.6.3 Improve read_byte_from_file() as fseek and ftell not required (were needed for Retro-Printer Module implementation)
 *        Improve comments on MSB Setting to clarify usage
 * v1.6.4 - 8 bit characters are enabled by default (for international character sets for example) - you can change this by setting use8bitchars = 0
 *        - If you want to use the top range of characters as italics, set useItalicsCharSet to 1
 *        - Default margins for A4 paper sizes are now 10 mm each side and 20 mm on the left (portrait) or the top (landscape)
 *        - The name of the file to be converted can be passed on the command line as last argument
 *        - read_byte_from_file() didn't manipulate the data but the address of the data
 *        - Superscript and subscript should look better now
 *        - Bug with unexpected infinite loop fixed
 *
 * v1.7 - Flexible command line control (see function usage() for more information)
 *      - Fix right margin issue (margin was ignored)
 *      - Load font data in one task calling fread()
 *      - Introduce quiet mode
 * v1.8 - Fixed some errors in graphics printing and double height text
 * v1.9 - Fixed potential errors with negative movements
 * v1.10- Fixed errors in the Delta Row Printing Mode
 * v1.11- Added missing definition for resolution360
 *
 * www.retroprinter.com
 *
 * Relies on libpng and ImageMagick libraries
 *
 * Incoming data (if Epson ESC/P or ESC/P2) is used to generate a bitmap.
 * Without any library to convert the bitmap directly to PDF, we have to convert the bitmap to PNG initially and then
 * convert the PNG to a PDF file using the libHaru library
 */

// START OF CONFIGURATION OPTIONS
// Configuration files - these are simple files with flags
#define LINEFEED_CONFIG     "/root/config/linefeed_ending"          // Unix, Windows or Mac line feeds in conversion
#define PATH_CONFIG         "/root/config/output_path"              // default path for output files

int cpi = 10;                               // PICA is standard
int pitch = 10;                             //Same as cpi but will retain its value when condensed printing is switched on
int needles = 24;                           // number of needles - 9 pin can be important for line spacing
int letterQuality = 0;                      // LQ Mode?
int proportionalSpacing = 0;                // Proportional Mode (not implemented)
int imageMode = 1;                          // Whether to use faster in memory conversion or file conversion
int colourSupport = 6;                      // Does the ESC.2 / ESC.3 mode support 4 colour (CMYK) or 6 colour (CMYK + Light Cyan, Light Magenta) ?
int auto_LF = 0;                            // Whether we should process a CR on its own as a line feed
int step = 0;
// END OF CONFIGURATION OPTIONS

int use8bitchars = 0;                       // Use 8 bit character sets - for example for umlaut characters ASCII 160-255 are treated as normal characters (see Italics too)
int useItalicsCharSet = 0;                  // Whether characters with codes ASCII 160-255 are to be treated as italics (do not use with use8bitchars)
int quiet_mode = 0;

int pageSetWidth;
int pageSetHeight;
const float printerdpih = 720.0;
const float printerdpiv = 720.0;
float hmi = (float) 720 * ((float) 36 / (float) 360);              // pitch in inches per character.  Default is 36/360 * 720 = 10 cpi
int line_spacing = (float) 720 * ((float) 1 / (float) 6);     // normally 1/6 inch line spacing - (float) 30*((float)pitch/(float)cpi);
int cdpih = 120;                            // fixed dots per inch used for printing characters
int cdpiv = 144;                            // fixed dots per inch used for printing characters
int cpih = 10;                              // Default is PICA
int cpiv = 6;                               // Default font height in cpi (default line spacing is 1/6" inch too)
int dpih = 180, dpiv = 180;                 // resolution in dpi for ESC/P2 printers

const float resolution360 = 1 / (float) 360; 

// Space used for storage - printermemory holds the bitmap file generated from the captured data
// seedrow is used for enhanced ESC/P2 printer modes where each line is based on preceding line
// imagememory is used to store the PNG file (generated from the bitmap).
unsigned char *printermemory, *seedrow, *imagememory;
int defaultMarginLeftp,defaultMarginRightp,defaultMarginTopp,defaultMarginBottomp;
int marginleft = 0, marginright = 99;       // in characters
int marginleftp, marginrightp;              // in pixels
int margintopp, marginbottomp;

unsigned int page = 0;
char filenameX[1000];
char filenameY[1000];
int xdim, ydim;
int sdlon = 0;              // sdlon=1 Copy output to SDL screen
int state = 1;              // State of the centronics interface
int countcharz;

char    path[1000];         // main path
char    pathraw[1000];      //path to raw files
char    pathpng[1000];      //path to png files
char    pathpdf[1000];      //path to pdf files
char    patheps[1000];      //path to eps files

// colour table for quick lookup to convert printer colours to RGB
char red[129], green[129], blue[129];
int WHITE = 128;                        // Bit 7 is set for a white pixel in printermemory

int t1=0,t2=0,t3=0,t4=0,t5=0;
int ackposition            = 0;
int msbsetting             = 0;
int outputFormatText       = 0;         //Used to determine whether to convert line endings in captured file 0=no conversion  1=Unix (LF) 2= Windows (CR+LF) 3=MAC (CR)
int printColour            = 0;         //Default colour is black
double defaultUnit         = 0;         //Use Default defined unit
double thisDefaultUnit     = 0;         //Default unit for use by command
double pageManagementUnit  = 0;         //Default Page Management Unit - extended ESC ( U
double relHorizontalUnit   = 0;         //Default Relative Horizontal Management Unit - extended ESC ( U
double absHorizontalUnit   = 0;         //Default Absolute Horizontal Management Unit - extended ESC ( U
double relVerticalUnit     = 0;         //Default Relative Vertical Management Unit - extended ESC ( U
double absVerticalUnit     = 0;         //Default Absolute Vertical Management Unit - extended ESC ( U
int useExtendedSettings    = 0;         //Do we use normal settings for default unit or extended set?

int bold                   = 0;         //Currently bold and double-strike are the same
int underlined             = 0;
int italic                 = 0;
int superscript            = 0;
int subscript              = 0;
int strikethrough          = 0;
int overscore              = 0;
int double_width           = 0;         //Double width printing
int double_width_single_line = 0;
int double_height          = 0;         //Double height printing
int quad_height            = 0;         // 4 x Height Printing - Star NL-10
int outline_printing       = 0;         //Outline printing not yet implemeneted
int shadow_printing        = 0;         //Shadow printing not yet implemented

int print_controlcodes     = 0;
int print_uppercontrolcodes= 0;

int graphics_mode          = 0;
int microweave_printing    = 0;
int multipoint_mode        = 0;
int escKbitDensity         = 0;         // 60 dpi
int escLbitDensity         = 1;         // 120 dpi
int escYbitDensity         = 2;         // 120 dpi
int escZbitDensity         = 3;         // 240 dpi

SDL_Surface *display;
jmp_buf env;

FILE *inputFile;

int isNthBitSet (unsigned char c, int n) {
    return (c >> n) & 1;
}

// CMYK Colour mixing method
double * rgb_to_cmyk(int rgb_red, int rgb_green, int rgb_blue)
{
    static double cmyk_value[4];
    double rgb_scale = 255, cmyk_scale = 100, min_cmy;

    cmyk_value[0] = 0;
    cmyk_value[1] = 0;
    cmyk_value[2] = 0;
    cmyk_value[3] = 0;

    if ((rgb_red == 0) && (rgb_green == 0) && (rgb_blue == 0)) {
        // black
        return cmyk_value;
    }

    // Change rgb [0,255] -> cmy [0,1]
    cmyk_value[0] = 1 - ((double) rgb_red / rgb_scale);
    cmyk_value[1] = 1 - ((double) rgb_green / rgb_scale);
    cmyk_value[2] = 1 - ((double) rgb_blue / rgb_scale);

    // extract out k [0,1]
    min_cmy = cmyk_value[0];
    if (cmyk_value[1] < min_cmy) min_cmy = cmyk_value[1];
    if (cmyk_value[2] < min_cmy) min_cmy = cmyk_value[2];
    cmyk_value[0] = (cmyk_value[0] - min_cmy);
    cmyk_value[1] = (cmyk_value[1] - min_cmy);
    cmyk_value[2] = (cmyk_value[2] - min_cmy);
    cmyk_value[3] = min_cmy;

    // rescale to the range [0,cmyk_scale]
    cmyk_value[0] = cmyk_value[0] * cmyk_scale;
    cmyk_value[1] = cmyk_value[1] * cmyk_scale;
    cmyk_value[2] = cmyk_value[2] * cmyk_scale;
    cmyk_value[3] = cmyk_value[3] * cmyk_scale;

    return cmyk_value;
}
int * cmyk_to_rgb(double cmyk_c, double cmyk_m, double cmyk_y, double cmyk_k)
{
    static int rgb_value[3]; // test
    double rgb_scale = 255, cmyk_scale = 100;

    rgb_value[0] = rgb_scale*(1.0-(cmyk_c+cmyk_k)/cmyk_scale);
    rgb_value[1] = rgb_scale*(1.0-(cmyk_m+cmyk_k)/cmyk_scale);
    rgb_value[2] = rgb_scale*(1.0-(cmyk_y+cmyk_k)/cmyk_scale);
    return rgb_value;
}

int * cmykColourMix(int rgb_red1, int rgb_green1, int rgb_blue1, int rgb_red2, int rgb_green2, int rgb_blue2)
{
    static int rgb_result[3];
    int *rgb_lookups;
    double *cmyk, cmyk_result_c, cmyk_result_m, cmyk_result_y, cmyk_result_k, opacity = 0.5;
    cmyk_result_c = 0;
    cmyk_result_m = 0;
    cmyk_result_y = 0;
    cmyk_result_k = 0;

    cmyk = rgb_to_cmyk(rgb_red1, rgb_green1, rgb_blue1);
    cmyk_result_c += opacity * cmyk[0];
    cmyk_result_m += opacity * cmyk[1];
    cmyk_result_y += opacity * cmyk[2];
    cmyk_result_k += opacity * cmyk[3];

    cmyk = rgb_to_cmyk(rgb_red2, rgb_green2, rgb_blue2);
    cmyk_result_c += opacity * cmyk[0];
    cmyk_result_m += opacity * cmyk[1];
    cmyk_result_y += opacity * cmyk[2];
    cmyk_result_k += opacity * cmyk[3];

    rgb_lookups = cmyk_to_rgb(cmyk_result_c, cmyk_result_m, cmyk_result_y, cmyk_result_k);
    rgb_result[0] = rgb_lookups[0];
    rgb_result[1] = rgb_lookups[1];
    rgb_result[2] = rgb_lookups[2];
    return rgb_result;
}

int * lookupColour(unsigned char colourValue)
{
    // Convert printer colour (0 to 7 stored in bits of colourValue) to RGB value
    // Routine uses averaging to get colours such as pink (red + white)
    static int rgb1[3];
    int colourMixMethod = 2; // 1 = RGB simple addition method, 2 = Standard CMYK Mix with conversion -
    int *mixedColour;
    int mixedColour_red, mixedColour_green, mixedColour_blue;
    rgb1[0]=0;
    rgb1[1]=0;
    rgb1[2]=0;
    if (colourValue == 1) return rgb1; // Black
    int mixColour = 0;
    if (colourValue == 0 || colourValue == WHITE) {
        // Bit 7 is set for white - not supported on printers
        rgb1[0]=255;
        rgb1[1]=255;
        rgb1[2]=255;
        return rgb1;
    }
    if (isNthBitSet(colourValue, 0) ) {
        // Black - default
        mixColour = 1;
    }

    mixedColour_red = rgb1[0];
    mixedColour_green = rgb1[1];
    mixedColour_blue = rgb1[2];

    if (isNthBitSet(colourValue, 1) ) {
        // Magenta FF00FF
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 255) /2;
                mixedColour_green = (mixedColour_green + 0) /2;
                mixedColour_blue = (mixedColour_blue + 255) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 255, 0, 255);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 255;
            mixedColour_green = 0;
            mixedColour_blue = 255;
        }
        mixColour = 1;
    }
    if (isNthBitSet(colourValue, 2) ) {
        // Cyan 00FFFF
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 0) /2;
                mixedColour_green = (mixedColour_green + 255) /2;
                mixedColour_blue = (mixedColour_blue + 255) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 0, 255, 255);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 0;
            mixedColour_green = 255;
            mixedColour_blue = 255;
        }
        mixColour = 1;
    }
    if (isNthBitSet(colourValue, 3) ) {
        // Violet EE82EE
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 238) /2;
                mixedColour_green = (mixedColour_green + 130) /2;
                mixedColour_blue = (mixedColour_blue + 238) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 238, 130, 238);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 238;
            mixedColour_green = 130;
            mixedColour_blue = 238;
        }
        mixColour = 1;
    }
    if (isNthBitSet(colourValue, 4) ) {
        // Yellow FFFF00
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 255) /2;
                mixedColour_green = (mixedColour_green + 255) /2;
                mixedColour_blue = (mixedColour_blue + 0) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 255, 255, 0);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 255;
            mixedColour_green = 255;
            mixedColour_blue = 0;
        }
        mixColour = 1;
    }
    if (isNthBitSet(colourValue, 5) ) {
        // Red FF0000
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 255) /2;
                mixedColour_green = (mixedColour_green + 0) /2;
                mixedColour_blue = (mixedColour_blue + 0) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 255, 0, 0);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 255;
            mixedColour_green = 0;
            mixedColour_blue = 0;
        }
        mixColour = 1;
    }
    if (isNthBitSet(colourValue, 6) ) {
        // Green 00FF00
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 0) /2;
                mixedColour_green = (mixedColour_green + 255) /2;
                mixedColour_blue = (mixedColour_blue + 0) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 0, 255, 0);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 0;
            mixedColour_green = 255;
            mixedColour_blue = 0;
        }
    }
    /* For CMYKlMlC printing we have to support 10 bits - not implemented as it would double memory used for only a small subset of later printers
       If necessary, would be better to have flag to say if 6 colour printing desired (although some large format printers support 8 colours)
       and could then renumber the bits and use WHITE = 0
    if (isNthBitSet(colourValue, 9) ) {
        // Light Magenta FF80FF
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 255) /2;
                mixedColour_green = (mixedColour_green + 128) /2;
                mixedColour_blue = (mixedColour_blue + 255) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 255, 128, 255);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 255;
            mixedColour_green = 128;
            mixedColour_blue = 255;
        }
        mixColour = 1;
    }
    if (isNthBitSet(colourValue, 10) ) {
        // Light Cyan 80FFFF
        if (mixColour == 1) {
            if (colourMixMethod == 1) {
                mixedColour_red = (mixedColour_red + 128) /2;
                mixedColour_green = (mixedColour_green + 255) /2;
                mixedColour_blue = (mixedColour_blue + 255) /2;
            } else {
                mixedColour = cmykColourMix(mixedColour_red, mixedColour_green, mixedColour_blue, 128, 255, 255);
                mixedColour_red = mixedColour[0];
                mixedColour_green = mixedColour[1];
                mixedColour_blue = mixedColour[2];
            }
        } else {
            mixedColour_red = 128;
            mixedColour_green = 255;
            mixedColour_blue = 255;
        }
    }
    */
    rgb1[0] = mixedColour_red;
    rgb1[1] = mixedColour_green;
    rgb1[2] = mixedColour_blue;
    return rgb1;
}

void setupColourTable()
{
    // Create a lookup table to look up all of the possible colour combinations and convert to RGB for quicker PNG generation
    int j, *pixelColour;
    for (j = 0; j<=128; j++) {
        pixelColour = lookupColour(j);
        red[j] = pixelColour[0];
        green[j] = pixelColour[1];
        blue[j] = pixelColour[2];
    }
}

void erasepage()
{
    //clear memory
    memset(printermemory, WHITE , pageSetWidth * pageSetHeight);
}

int initialize(const char* input_filename)
{
    // Choose PNG Generation mode - 1 = in memory (fast but uses a lot more memory), 2 = use external file (slower, but less memory)
    imageMode = 1;

    marginleftp = defaultMarginLeftp;
    marginrightp = defaultMarginRightp;   // in pixels
    margintopp = defaultMarginTopp;
    marginbottomp = defaultMarginBottomp;

    // Set aside enough memory to store the parsed image
    printermemory = malloc ((pageSetWidth+1) * pageSetHeight);
    if (printermemory == NULL) {
        fputs("Can't allocate memory for Printer Conversion.\n", stderr);
        exit (1);
    }
    // For Delta Row compression - set aside room to store 4 seed rows (1 per supported colour)
    if (imageMode == 1 ) {
        // Faster method of creating and converting PNG image - stores it in memory, so needs a lot more memory
        imagememory = calloc (3 * (pageSetWidth+1) * pageSetHeight, 1);
        if (imagememory == NULL) {
            free(printermemory);
            printermemory=NULL;
            fputs("Can't allocate memory for PNG image.\n", stderr);
            exit (1);
        }
        // For Delta Row compression - set aside room to store colourSupport (4 or 6) seed rows (1 per supported colour)
        // May as well use the imagememory temporarily for the seedrows
        seedrow = imagememory;
    } else {
        // Slower method - PNG image is saved to disk first and then converted from there
        seedrow = calloc ((pageSetWidth+1) * colourSupport, 1);
        if (seedrow == NULL) {
            free(printermemory);
            printermemory=NULL;
            fputs("Can't allocate memory for Delta Row Printing.\n", stderr);
            exit (1);
        }
    }
    erasepage();
    setupColourTable();
    /* routine could be used here to open the input file or port for reading
    *  example is for reading from an input file called ./Test1.prn
    *  The routine is not error trapped at present
    */
    inputFile = fopen(input_filename, "r");
    if (inputFile == NULL)
    {
        fprintf(stderr, "Failed to open input file: '%s'\n", input_filename);
        return -1;
    }

}

int read_byte_from_file (char *xd)
{
    // This needs to be written to read each byte from specified file

    *xd=fgetc(inputFile);

    switch (msbsetting) {
        case 0:
            // No change
            break;
        case 1:
            // MSB setting clears bit 7
            *xd = (int) *xd & 127;
            break;
        case 2:
            // MSB setting forces bit 7 to 1
            *xd = (int) *xd | 128;
            break;
    }

    return feof(inputFile) ? 0 : -1;
}

/*
 * Check if a file exist using stat() function
 * return 1 if the file exist otherwise return 0
 */
int cfileexists(const char* filename)
{
    struct stat   buffer;
    return (stat (filename, &buffer) == 0);
}

int write_png(const char *filename, int width, int height, char *rgb)
{
    int ipos, data_found = 0, end_loop = 0;
    unsigned char pixelColour;
    int code = 1;
    //Used only if (imageMode == 2 )
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep row = NULL;
    //
    FILE *file = NULL;

    // Check if a blank page - if so ignore it!
    int x, y, ppos;
    for (y=0 ; y<height && !data_found; y++) {
        ipos = width * y;
        for (x=0 ; x<width && !data_found; x++) {
            if (rgb[ipos+x] != WHITE) data_found = 1;
        }
    }
    if (!data_found) return 0; // Nothing to print

    if (imageMode == 1 ) {
        // Create raw image in memory for speed
        // Write image data - 8 bit RGB
        // Create raw image in memory
        ppos=0;
        for (y=0 ; y<height ; y++) {
            ipos = width * y;
            for (x=0 ; x<width ; x++) {
                pixelColour = rgb[ipos + x];
                imagememory[ppos++] = red[pixelColour];
                imagememory[ppos++] = green[pixelColour];
                imagememory[ppos++] = blue[pixelColour];
            }
        }
    } else {
        // Use LibPNG to create a PNG image on disk for conversion
        if (!quiet_mode) printf("write   = %s \n", filenameX);
        // Open file for writing (binary mode)
        file = fopen(filename, "wb");
        if (file == NULL) {
            fprintf(stderr, "PNG error - Could not open file %s for writing\n", filename);
            code = 0;
            goto finalise;
        }
        // Initialize write structure
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (png_ptr == NULL) {
            fputs("PNG error - Could not allocate write structure\n", stderr);
            code = 0;
            goto finalise;
        }
        // Initialize info structure
        info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL) {
            fputs("PNG error - Could not allocate info structure\n", stderr);
            code = 0;
            goto finalise;
        }

        png_set_compression_level(png_ptr, 6);  // Minimal compression for speed - balance against time taken by convert routine
        // Setup Exception handling
        if (setjmp(png_jmpbuf(png_ptr))) {
            fputs("Error during png creation\n", stderr);
            code = 0;
            goto finalise;
        }

        png_init_io(png_ptr, file);

        // Write header (8 bit colour depth)
        png_set_IHDR(png_ptr, info_ptr, width, height,
                    8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                    PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        // Set the resolution of the image to 720dpi
        png_set_pHYs(png_ptr, info_ptr, printerdpih/0.0254, printerdpiv/0.0254,
            PNG_RESOLUTION_METER);

        png_write_info(png_ptr, info_ptr);
        // Allocate memory for a rows (3 bytes per pixel - 8 bit RGB)
        row = (png_bytep) malloc(3 * width * sizeof(png_byte));
        if (row == NULL) {
            fputs("Can't allocate memory for PNG file.\n", stderr);
            code = 0;
            goto finalise;
        }

        // Write image data - 8 bit RGB
        for (y=0 ; y<height ; y++) {
            ipos = width * y;
            ppos=0;
            for (x=0 ; x<width ; x++) {
                pixelColour = rgb[ipos + x];
                row[ppos++] = red[pixelColour];
                row[ppos++] = green[pixelColour];
                row[ppos++] = blue[pixelColour];
            }
            png_write_row(png_ptr, row);
        }

        // End write
        png_write_end(png_ptr, NULL);
    }

finalise:
    if (imageMode == 1 ) {
        // No need to free up memory - will reuse existing memory
    } else {
        // Tidy up LibPNG accesses
        if (file != NULL) fclose(file);
        if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
        if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        if (row != NULL) free(row);
    }
    return code;
}

void pdf_error_handler (HPDF_STATUS error_no, HPDF_STATUS detail_no, void *user_data)
{
    fprintf(stderr, "ERROR: error_no=%04X, detail_no=%u\n", (HPDF_UINT)error_no, (HPDF_UINT)detail_no);
    longjmp(env, 1);
}

HPDF_REAL ScaleDPI(HPDF_REAL size)
{
    return (float) size * (72.0F / (float) printerdpih);
}

int write_pdf (const char *filename, const char *pdfname, int width, int height)
{
    HPDF_Doc  pdf;
    HPDF_Font font;
    HPDF_Page page;
    HPDF_Destination dst;

    pdf = HPDF_New (pdf_error_handler, NULL);
    if (!pdf) {
        fputs("error: cannot create PdfDoc object\n", stderr);
        return 1;
    }

    /* error-handler */
    if (setjmp(env)) {
        HPDF_Free (pdf);
        return 1;
    }

    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);

    /* create default-font */
    font = HPDF_GetFont (pdf, "Helvetica", NULL);

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    HPDF_Page_SetWidth (page, ScaleDPI(width));
    HPDF_Page_SetHeight (page, ScaleDPI(height));

    dst = HPDF_Page_CreateDestination (page);
    HPDF_Destination_SetXYZ (dst, 0, HPDF_Page_GetHeight (page), 1);
    HPDF_SetOpenAction(pdf, dst);

    /*
    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, font, 20);
    HPDF_Page_MoveTextPos (page, 220, HPDF_Page_GetHeight (page) - 70);
    HPDF_Page_ShowText (page, "PngDemo");
    HPDF_Page_EndText (page);
    HPDF_Page_SetFontAndSize (page, font, 12);
    */

    HPDF_Image image;

    if (imageMode == 1 ) {
        image = HPDF_LoadRawImageFromMem (pdf, imagememory,
                width, height, HPDF_CS_DEVICE_RGB, 8);
    } else {
        image = HPDF_LoadPngImageFromFile (pdf, filename);
    }

    /* Draw image to the canvas. */
    HPDF_Page_DrawImage (page, image, 0, 0, ScaleDPI(width),
                    ScaleDPI(height));

    /* save the document to a file */
    HPDF_SaveToFile (pdf, pdfname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

void putpx(int x, int y)
{
    // Write printer colour to specific pixel on the created bitmap
    int pos = y * pageSetWidth + x;
    unsigned char existingPixel = printermemory[pos];

    // If existing pixel is white, then we need to reset it to 0 before OR'ing the chosen colour
    if (existingPixel == WHITE) {
        printermemory[pos] = (1 << printColour);
    } else {
        printermemory[pos] |= (1 << printColour);
    }
}

/*
 * Set the pixel at (x, y) to the given value
 * NOTE: The surface must be locked before calling this!
 */
static float divi = 1.0;  // divider for lower resolution
void putpixel(SDL_Surface * surface, int x, int y, Uint32 pixel)
{
    // if we are out of scope don't putpixel, otherwise we'll get a segmentation fault
    if (x > (pageSetWidth - 1)) return;
    if (y > (pageSetHeight - 1)) return;
    putpx(x, y);    // Plot to bitmap for PDF
    if (sdlon == 0) return;

    // Add pixel to the screen
    x = x / divi;
    y = y / divi;
    if (x > xdim) return;
    if (y > ydim) return;
    int bpp = surface->format->BytesPerPixel;

    // Here p is the address to the pixel we want to set
    Uint8 *p = (Uint8 *) surface->pixels + y * surface->pitch + x * bpp;

    // Convert the otherwise black pixel to the desired colour
    if (pixel == 0x00000000) {
        switch (printColour) {
        case 0:
            // Black
            pixel = 0x00000000;
            break;
        case 1:
            // Magenta
            pixel = 0x00FF00FF;
            break;
        case 2:
            // Cyan
            pixel = 0x0000FFFF;
            break;
        case 3:
            // Violet
            pixel = 0x00EE82EE;
            break;
        case 4:
            // Yellow
            pixel = 0x00FFFF00;
            break;
        case 5:
            // Red
            pixel = 0x00FF0000;
            break;
        case 6:
            // Green
            pixel = 0x0000FF00;
            break;
        case 7:
            // White
            pixel = 0x00FFFFFF;
            break;
        }
    }

    // If existing pixel is 0x00FFFFFF (white), then we need to reset it to 0x00000000; before OR'ing the chosen colour
    switch (bpp) {
    case 1:
        if (p[0] == 255) {
            p[0] = pixel;
        } else {
            p[0] |= pixel;
        }
        break;
    case 2:
        if (p[0] == 255 && p[1] == 255) {
            p[0] = pixel;
            p[1] = pixel;
        } else {
            *(Uint16 *) p |= pixel;
        }
        break;
    case 3:
        if (p[0] == 255 && p[1] == 255 && p[2] == 255) {
            p[0] = pixel;
            p[1] = pixel;
            p[2] = pixel;
        } else if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] |= (pixel >> 16) & 0xff;
            p[1] |= (pixel >> 8) & 0xff;
            p[2] |= pixel & 0xff;
        } else {
            p[0] |= pixel & 0xff;
            p[1] |= (pixel >> 8) & 0xff;
            p[2] |= (pixel >> 16) & 0xff;
        }
        break;
    case 4:
        if (p[0] == 255 && p[1] == 255 && p[2] == 255 && p[3] == 255) {
            *(Uint32 *) p = pixel;
        } else {
            *(Uint32 *) p |= pixel;
        }
        break;
    }
}

void putpixelbig(int xpos, int ypos, int hwidth, int vdith)
{
    int a, b;
    for (a = 0; a < hwidth; a++) {
        for (b = 0; b < vdith; b++) {
            putpixel(display, xpos + a, ypos + b, 0x00000000);
        }
    }
}

int rows = 0;
int xpos = 0, ypos = 0;                     // position of print head
int xpos2 = 0, ypos2 = 0;                   // position of print head

float chrSpacing = 0;                      // Inter-character spacing

// ******bit images
int m;                                      // Mode for bit images printing (m)
int m1, m2, m3, m4;                         // Used for extended commands
int c, v, h;                                // Used for raster graphics printing
unsigned char nL, nH;                       // width of bit image line, lowbyte and high byte
unsigned char mL, mH;                       // extra parameters mode, lowbyte and high byte
unsigned char tL, tH;                       // extra parameters top margin, lowbyte and high byte
unsigned char bL, bH;                       // extra parameters bottom margin, lowbyte and high byte
unsigned char d1, d2, d3;
unsigned char a0, a1, a2;
int advance;                                // Used to calculate negative movements
int dotColumns;                             // number of dot columns for bit images

// Tabs
double hTabulators[35];                     // List of tabulators
double vTabulators[16 * 8];                 // List of vertical tabulators
int vTabulatorsSet = 0;                     // Used by VT command to check if any vertical tabs have been set since printer reset
int vTabulatorsCancelled = 0;               // Used by VT command to check if all vertical tabs have been cancelled
int vFUChannel = 0;                         // VFU Channel to use
int curHtab = 0;                            // next active horizontal tab
int curVtab = 0;                            // next active vertical tab

int hPixelWidth, vPixelWidth;
FILE *f;
FILE *fp = NULL;

#define FONT_SIZE  4096
char fontx[FONT_SIZE];

void erasesdl()
{
    int i, t;
    if (sdlon == 0) return;
    // pageSetWidth*pageSetHeight
    for (i = 0; i < pageSetWidth; i++) {
        for (t = 0; t < pageSetHeight; t++) {
            putpixel(display, i, t, 0x00FFFFFF);
        }
    }
}

int test_for_new_paper()
{
    // if we are out of paper
    if ((ypos < margintopp) || (ypos > marginbottomp) || (state == 0)) {
        xpos = marginleftp;
        ypos = margintopp;
        sprintf(filenameX, "%spage%d.png", pathpng, page);
        if (write_png(filenameX, pageSetWidth, pageSetHeight, printermemory) > 0) {
            // Create pdf file
            sprintf(filenameY, "%spage%d.pdf", pathpdf, page);
            write_pdf(filenameX, filenameY, pageSetWidth, pageSetHeight);
            erasesdl();
            erasepage();
            page++;
            if (page > 199) {
                page = dirX(pathraw);
                reduce_pages(page, pathraw);
                page = dirX(pathpng);
                reduce_pages(page, pathpng);
                page = dirX(pathpdf);
                reduce_pages(page, pathpdf);
                page = dirX(pathpdf) + 1;
            }
        }
        if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
    }
    state = 1;
}

int precedingDot(int x, int y) {
    int pos = (y * pageSetWidth) + (x-1);
    if (printermemory[pos] == WHITE) return 0;
    return 1;
}

void _clear_seedRow(int seedrowColour) {
    // colourSupport seedrows - each pixel is represented by a bit.
    if (seedrowColour > (colourSupport-1)) seedrowColour = colourSupport-1;
    memset(seedrow + ((seedrowColour * pageSetWidth) /8), 0 , (pageSetWidth / 8));
}

void _print_seedRows(float hPixelWidth, float vPixelWidth){   
    int store_colour, seedrowColour, bytePointer, seedrowStart, seedrowEnd;
    int byteOffset, bitOffset, xByte;
    unsigned char xd;
    // Copy all seed data across to new row
    store_colour = printColour;
    xpos = marginleftp;
    for (seedrowColour = 0; seedrowColour < colourSupport; seedrowColour++) {
        printColour = seedrowColour;
        switch (seedrowColour) {
            case 3:
                printColour = 4; // Yellow
                break;
            case 4:
                printColour = 9; // Light Magenta
                break;
            case 5:
                printColour = 10; // Light Cyan
                break;
        }        
        seedrowStart = (seedrowColour * pageSetWidth) /8;
        seedrowEnd = seedrowStart + (pageSetWidth / 8);
        bytePointer = seedrowStart + (xpos / 8);
        bitOffset = 7 - ((int) xpos & 7); // Bit & 7 is cheaper than remainder % 8
        for (byteOffset = bytePointer; byteOffset < seedrowEnd; byteOffset++) {
            xd = seedrow[byteOffset];            
            if (xd > 0) {
                for (xByte = bitOffset; xByte >= 0; xByte--) {
                    if (isNthBitSet(xd,xByte)) {
                        putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                    }
                    xpos += hPixelWidth;
                }
            } else {
                xpos += hPixelWidth * (bitOffset + 1);
            }
            bitOffset=7;
        }
        xpos = marginleftp;
    }
    printColour = store_colour;
}

void _print_incomingDataByte(int compressMode, unsigned char xd, int seedrowStart, int seedrowEnd, float hPixelWidth, float vPixelWidth) {
    int byteOffset, bitOffset, xByte;
    byteOffset = seedrowStart + (xpos / 8);
    bitOffset = 7 - ((int) xpos & 7);            // Bit & 7 is cheaper than remainder % 8
    if (xd == 0) {
        if (compressMode == 3) {
            // for ESC.3 Delta Row clear bits in seedrow for current colour
            for (xByte = 0; xByte < 8; xByte++) {
                if (byteOffset < seedrowEnd) {
                    // Clear bit in the seed row
                    seedrow[byteOffset] &= ~(1 << bitOffset);
                    if (bitOffset == 0) {
                        byteOffset++;
                        bitOffset = 7;
                    } else {
                        bitOffset--;
                    }
                } else {
                    break;
                }
            }
        }
        xpos += hPixelWidth * 8;
    } else {
        for (xByte = 0; xByte < 8; xByte++) {
            if (compressMode == 3) {
                if (byteOffset < seedrowEnd) {
                    if (xd & 128) {
                        // Set bit in the seed row
                        seedrow[byteOffset] |= (1 << bitOffset);
                    } else {
                        // Clear bit in the seed row
                        seedrow[byteOffset] &= ~(1 << bitOffset);
                    }
                    if (bitOffset == 0) {
                        byteOffset++;
                        bitOffset = 7;
                    } else {
                        bitOffset--;
                    }
                } else {
                    break;
                }
            } else {
                if (xd & 128) {
                    // Draw it on screen
                    putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                }
            }
            xd = xd << 1;
            xpos += hPixelWidth;
        }
    }
}

int _is_little_endian_machine()
{
  unsigned int x = 1;
  char *c = (char*) &x;
  return (int)*c;
}

void _tiff_delta_printing(int compressMode, float hPixelWidth, float vPixelWidth) {
    int opr, xByte, j, bytePointer, colour, existingColour, counterValue, repeatValue;
    unsigned char xd, repeater, command, dataCount;
    signed int parameter;
    int byteOffset, bitOffset;
    int moveSize = 1; // Set by MOVEXDOT or MOVEXBYTE
    int seedrowColour = 5;
    int seedrowStart, seedrowEnd, colourLoop;
    if (compressMode == 3) {
        // Delta Row Compression - clear all seed rows
        for (j = 0; j <= seedrowColour; j++) {
            _clear_seedRow(j);
        }
    }
    existingColour = printColour;
    // Original ESC/P2 supports 4 colours, but later printers support 6 or even 8 !
    // Supported Colours are 0 = BLACK, 1 = MAGENTA, 2 = CYAN, 4 = YELLOW, 9 = LIGHT MAGENTA, 10 = LIGHT CYAN
    if (printColour > 4 && printColour < 9) printColour = 4; 
    if (printColour > 10) printColour = 10; 

  tiff_delta_loop:
    // Work out current seedrow for Delta Row Printing
    seedrowColour = printColour;
    switch (printColour) {
        case 4:
            seedrowColour = 3; // Colours are 0,1,2,4,9 & 10
            break;
        case 9:
            seedrowColour = 4; // Colours are 0,1,2,4,9 & 10
            break;
        case 10:
            seedrowColour = 5; // Colours are 0,1,2,4,9 & 10  
            break;
    }
    seedrowStart = (seedrowColour * pageSetWidth) / 8;
    seedrowEnd = seedrowStart + (pageSetWidth / 8);

    // Get command into nibbles
    state = read_byte_from_file((char *) &xd);  // byte1
    if (!state) goto raus_tiff_delta_print;
    command = 0;
    parameter = 0;    
    command = (unsigned char) xd >> 4;    
    if (_is_little_endian_machine()) {
        parameter = (unsigned char) xd & 0xF;
    } else {
        parameter = (unsigned char) xd & 0xF;
        parameter *= 1 << CHAR_BIT;
    }

    switch (command) {
        case 2:
            // XFER 0010 xxxx - parameter number of raster image data 0...15
            for (opr = 0; opr < parameter; opr++) {
                state = read_byte_from_file((char *) &repeater);  // byte1
                if (!state) goto raus_tiff_delta_print;
                if (repeater <= 127) {
                    repeater++;
                    // string of data byes to be printed
                    for (j = 0; j < repeater; j++) {
                        state = read_byte_from_file((char *) &xd);  // byte1
                        if (!state) goto raus_tiff_delta_print;
                        opr++;
                        _print_incomingDataByte(compressMode, xd, seedrowStart, seedrowEnd, hPixelWidth, vPixelWidth);
                        // SDL_UpdateRect(display, 0, 0, 0, 0);
                    }
                } else {
                    // Repeat following byte twos complement (repeater)
                    repeater = (256 - repeater) + 1;
                    state = read_byte_from_file((char *) &xd);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                    opr++;
                    for (j = 0; j < repeater; j++) {
                        _print_incomingDataByte(compressMode, xd, seedrowStart, seedrowEnd, hPixelWidth, vPixelWidth);
                        // SDL_UpdateRect(display, 0, 0, 0, 0);
                    }
                }
            }
            break;
        case 3:
            // XFER 0011 xxxx - parameter number of lookups to raster printer image data 1...2
            if (parameter == 1) {
                state = read_byte_from_file((char *) &dataCount);  // byte1
                if (!state) goto raus_tiff_delta_print;
                counterValue = (int) dataCount;
            } else {
                state = read_byte_from_file((char *) &nL);  // byte1
                if (!state) goto raus_tiff_delta_print;
                state = read_byte_from_file((char *) &nH);  // byte1
                if (!state) goto raus_tiff_delta_print;
                counterValue = nL + (256 * nH);
            }
            for (opr = 0; opr < counterValue; opr++) {
                state = read_byte_from_file((char *) &repeater);  // byte1
                if (!state) goto raus_tiff_delta_print;            
                repeatValue = (int) repeater;
                if (repeatValue <= 127) {
                    repeatValue++;
                    // string of data byes to be printed
                    for (j = 0; j < repeatValue; j++) {
                        state = read_byte_from_file((char *) &xd);  // byte1
                        if (!state) goto raus_tiff_delta_print;
                        opr++;
                        _print_incomingDataByte(compressMode, xd, seedrowStart, seedrowEnd, hPixelWidth, vPixelWidth);
                        // SDL_UpdateRect(display, 0, 0, 0, 0);                    
                    }
                } else {
                    // Repeat following byte twos complement (repeater)
                    repeatValue = (256 - repeater) + 1;
                    state = read_byte_from_file((char *) &xd);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                    opr++;
                    for (j = 0; j < repeatValue; j++) {
                        _print_incomingDataByte(compressMode, xd, seedrowStart, seedrowEnd, hPixelWidth, vPixelWidth);
                        // SDL_UpdateRect(display, 0, 0, 0, 0);
                    }
                }
            }
            break;
        case 4:
            // MOVX 0100 xxxx - space to move -8 to 7
            if (parameter > 0) {        
                if (parameter > 7) parameter = 7 - parameter;
                if (useExtendedSettings) {
                    // See ESC ( U command for unit
                    thisDefaultUnit = relHorizontalUnit;
                } else {
                    thisDefaultUnit = defaultUnit;
                }        
                if (defaultUnit == 0) thisDefaultUnit = printerdpih * resolution360; // Default for command is 1/360 inch units
                xpos2 = xpos + (parameter * moveSize * (int) thisDefaultUnit);
                if (xpos2 >= marginleftp && xpos2 <= marginrightp) xpos = xpos2;
            }
            break;
        case 5:    
            // MOVX 0101 xxxx - parameter number of lookups to movement data 1...2
            if (parameter > 0) {        
                if (parameter == 1) {
                    state = read_byte_from_file((char *) &parameter);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                    if (parameter > 127) parameter = 127 - parameter;
                } else {
                    state = read_byte_from_file((char *) &nL);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                    state = read_byte_from_file((char *) &nH);  // byte1
                    if (!state) goto raus_tiff_delta_print;    
                    parameter = nL + (256 * nH);
                    if (parameter >= 32768) parameter = (128 * 256) - parameter;
                }
                if (useExtendedSettings) {
                    // See ESC ( U command for unit
                    thisDefaultUnit = relHorizontalUnit;
                } else {
                    thisDefaultUnit = defaultUnit;
                }        
                if (defaultUnit == 0) thisDefaultUnit = printerdpih * resolution360; // Default for command is 1/360 inch units
                xpos2 = xpos + (parameter * moveSize * (int) thisDefaultUnit);
                if (xpos2 >= marginleftp && xpos2 <= marginrightp) xpos = xpos2;
            }
            break;
        case 6:
            // MOVY 0110 xxxx - space to move down 0 to 15 units
            if (compressMode == 3) _print_seedRows(hPixelWidth, vPixelWidth);
            if (parameter > 0) {
                if (useExtendedSettings) {
                    // See ESC ( U command for unit
                    thisDefaultUnit = relVerticalUnit;
                } else {
                    thisDefaultUnit = defaultUnit;
                }        
                if (defaultUnit == 0) thisDefaultUnit = printerdpiv * resolution360; // Default for command is 1/360 inch units
                ypos += parameter * (int) thisDefaultUnit;
                test_for_new_paper(0);
            }        
            xpos = marginleftp;
            break;
        case 7:
            // MOVY 0111 xxxx - space to move down X dots
            if (compressMode == 3) _print_seedRows(hPixelWidth, vPixelWidth);
            if (parameter > 0) {
                if (parameter == 1) {
                    state = read_byte_from_file((char *) &parameter);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                } else {
                    state = read_byte_from_file((char *) &nL);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                    state = read_byte_from_file((char *) &nH);  // byte1
                    if (!state) goto raus_tiff_delta_print;
                    parameter = nL + (256 * nH);
                }        
                if (useExtendedSettings) {
                    // See ESC ( U command for unit
                    thisDefaultUnit = relVerticalUnit;
                } else {
                    thisDefaultUnit = defaultUnit;
                }        
                if (defaultUnit == 0) thisDefaultUnit = printerdpiv * resolution360; // Default for command is 1/360 inch units        
                ypos += parameter * (int) thisDefaultUnit;
                test_for_new_paper(0);
            }
            xpos = marginleftp;
            break;
        case 8:
            // COLR 1000 xxxx
            printColour = parameter;
            xpos = marginleftp;
            break;
        case 14:
            switch (parameter) {
                case 1:
                    // CLR 1110 0001
                    if (compressMode == 3) {
                        // Clear seedrow for current colour
                        _clear_seedRow(seedrowColour);
                    }
                    break;
                case 2:
                    // CR 1110 0010
                    xpos = marginleftp;
                    break;
                case 3:
                    // EXIT 1110 0011
                    xpos = marginleftp;
                    goto raus_tiff_delta_print;
                    break;
                case 4:
                    // MOVEXBYTE 1110 0100
                    moveSize = 8;
                    xpos = marginleftp;
                    break;
                case 5:
                    // MOVEXDOT 1110 0101
                    moveSize = 1;
                    xpos = marginleftp;
                    break;
            }
            break;
    }
    goto tiff_delta_loop;

  raus_tiff_delta_print:
    printColour = existingColour;
    return;
}

void
_8pin_line_bitmap_print(int dotColumns, float hPixelWidth, float vPixelWidth,
     float xzoom, float yzoom, int adjacentDot)
{
    // bitmap graphics printing - prints bytes vertically
    int opr, fByte;
    unsigned char xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        state = 0;
        while (state == 0) {
            state = read_byte_from_file((char *) &xd);  // byte1
            if (state == 0) goto raus_8p;
        }

        if (xd) {
            for (fByte = ypos; fByte < ypos + 8 * vPixelWidth; fByte+= vPixelWidth) {
                if (xd & 128) {
                    if ((adjacentDot == 0) && (precedingDot(xpos, fByte) == 1)) {
                        // Miss out second of two consecutive horizontal dots
                    } else {
                        putpixelbig(xpos, fByte, hPixelWidth, vPixelWidth);
                    }
                }
                xd = xd << 1;
            }
        }
        xpos += hPixelWidth;
    }
  raus_8p:
    return;
}

void
_9pin_line_bitmap_print(int dotColumns, float hPixelWidth, float vPixelWidth,
     float xzoom, float yzoom, int adjacentDot)
{
    // bitmap graphics printing - prints bytes vertically - special case for ESC ^ command
    int opr, fByte, xByte;
    unsigned char xd;
    float right_pos, byteWidth;
    right_pos = marginrightp - hPixelWidth * 8;    
    byteWidth = 8 * vPixelWidth;   
    test_for_new_paper(0);
    for (opr = 0; opr < dotColumns; opr++) {
        state = read_byte_from_file((char *) &xd);  // byte1
        if (state == 0) goto raus_9p;
        // If out of paper area on the right side, do nothing
        if (xpos <= right_pos) {         
            if (xd) {
                for (xByte = ypos; xByte < ypos + byteWidth; xByte+= vPixelWidth) {
                    if (xd & 128) {
                        if (!adjacentDot && precedingDot(xpos, xByte)) {
                            // Miss out second of two consecutive horizontal dots
                        } else {
                            putpixelbig(xpos, xByte, hPixelWidth, vPixelWidth);
                        }
                    }
                    xd = xd << 1;
                }
            }
            // Read pin 9
            state = read_byte_from_file((char *) &xd);  // byte2
            if (state == 0) goto raus_9p;
            if (xd & 128) {
                if (!adjacentDot && precedingDot(xpos, ypos + 8 * vPixelWidth)) {
                    // Miss out second of two consecutive horizontal dots
                } else {
                    putpixelbig(xpos, ypos + 8 * vPixelWidth, hPixelWidth, vPixelWidth);
                }
            }
            xpos += hPixelWidth;
        }
        // SDL_UpdateRect(display, 0, 0, 0, 0);
    }    
  raus_9p:
    return;
}

void
_24pin_line_bitmap_print(int dotColumns, float hPixelWidth, float vPixelWidth,
      float xzoom, float yzoom, int adjacentDot)
{
    // bitmap graphics printing - prints bytes vertically
    int opr, fByte, xByte;
    unsigned char xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        // print 3 bytes (3 x 8 dots) per column
        for (fByte = 0; fByte < 3; fByte++) {
            ypos2 = ypos + fByte * (8 * vPixelWidth);
            state = 0;

            while (state == 0) {
                state = read_byte_from_file((char *) &xd);  // byte1
                if (state == 0) goto raus_24p;
            }
            if (xd) {
                for (xByte = ypos2; xByte < ypos2 + 8 * vPixelWidth; xByte+= vPixelWidth) {
                    if (xd & 128) {
                        if ((adjacentDot == 0) && (precedingDot(xpos, xByte) == 1)) {
                            // Miss out second of two consecutive horizontal dots
                        } else {
                            putpixelbig(xpos, xByte, hPixelWidth, vPixelWidth);
                        }
                    }
                    xd = xd << 1;
                }
            }
        }
        xpos += hPixelWidth;
        // SDL_UpdateRect(display, 0, 0, 0, 0);
    }
  raus_24p:
    return;
}

void
_48pin_line_bitmap_print(int dotColumns, float hPixelWidth, float vPixelWidth,
      float xzoom, float yzoom, int adjacentDot)
{
    // bitmap graphics printing - prints bytes vertically
    int opr, fByte, xByte;
    unsigned char xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        // print 6 bytes (6 x 8 dots) per column
        for (fByte = 0; fByte < 6; fByte++) {
            ypos2 = ypos + fByte * (8 * vPixelWidth);
            state = 0;

            while (state == 0) {
                state = read_byte_from_file((char *) &xd);  // byte1
                if (state == 0) goto raus_48p;
            }
            if (xd) {
                for (xByte = ypos2; xByte < ypos2 + 8 * vPixelWidth; xByte+= vPixelWidth) {
                    if (xd & 128) {
                        if ((adjacentDot == 0) && (precedingDot(xpos, xByte) == 1)) {
                            // Miss out second of two consecutive horizontal dots
                        } else {
                            putpixelbig(xpos, xByte, hPixelWidth, vPixelWidth);
                        }
                    }
                    xd = xd << 1;
                }
            }
        }
        xpos += hPixelWidth;
        // SDL_UpdateRect(display, 0, 0, 0, 0);
    }
  raus_48p:
    return;
}

void
_line_raster_print(int bandHeight, int dotColumns, float hPixelWidth, float vPixelWidth,
     float xzoom, float yzoom, int rleEncoded)
{
    // Data is sent in horizontal bands of up to dotColumns high
    int opr, xByte, j, band, ypos2, byteCount;
    unsigned char xd, repeater;
    float right_pos;
    
    right_pos = marginrightp - vPixelWidth;
    test_for_new_paper();
    ypos2 = ypos;
    byteCount = dotColumns/8;
    
    for (band = 0; band < bandHeight; band++) {
        if (rleEncoded) {
            for (opr = 0; opr < dotColumns; opr++) {
                state = 0;
                while (state == 0) {
                    state = read_byte_from_file((char *) &repeater);  // number of times to repeat next byte
                    if (state == 0) goto raus_rasterp;
                }
                if (repeater <= 127) {
                    repeater++;
                    // string of data byes to be printed
                    for (j = 0; j < repeater; j++) {
                        state = 0;
                        while (state == 0) {
                            state = read_byte_from_file((char *) &xd);  // byte to be printed
                            if (state == 0) goto raus_rasterp;
                        }
                        if (xpos <= right_pos) {                   
                            if (xd) {
                                for (xByte = 0; xByte < 8; xByte++) {
                                    if (xpos <= right_pos) {
                                        if (xd & 128) putpixelbig(xpos, ypos2, hPixelWidth, vPixelWidth);
                                        xd = xd << 1;
                                        xpos += hPixelWidth;
                                    }
                                }
                            } else {
                                xpos += hPixelWidth * 8;
                            }
                        } else {
                            break;
                        }
                        opr++;
                        // SDL_UpdateRect(display, 0, 0, 0, 0);
                    }
                } else {
                    // Repeat following byte twos complement (repeater)
                    repeater = (256 - repeater) + 1;
                    state = 0;
                    while (state == 0) {
                        state = read_byte_from_file((char *) &xd);  // byte to be printed
                        if (state == 0) goto raus_rasterp;
                    }
                    for (j = 0; j < repeater; j++) {
                        if (xpos <= right_pos) {                   
                            if (xd) {
                                for (xByte = 0; xByte < 8; xByte++) {
                                    if (xpos <= right_pos) {
                                        if (xd & 128) putpixelbig(xpos, ypos2, hPixelWidth, vPixelWidth);
                                        xd = xd << 1;
                                        xpos += hPixelWidth;
                                    }
                                }
                            } else {
                                xpos += hPixelWidth * 8;
                            }    
                        } else {
                            break;
                        }
                    }
                    opr++;
                }
            }
        } else {
            for (opr = 0; opr < byteCount; opr++) {
                state = 0;
                while (state == 0) {
                    state = read_byte_from_file((char *) &xd);  // byte1
                    if (state == 0) goto raus_rasterp;
                }
                // If out of paper area on the right side, do nothing
                if (xd) {
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xpos <= right_pos) {                   
                            if (xd & 128) putpixelbig(xpos, ypos2, hPixelWidth, vPixelWidth);
                            xd = xd << 1;
                            xpos += hPixelWidth;
                        }
                    }
                } else {
                    if (xpos <= right_pos) xpos += hPixelWidth * 8;
                }
                // SDL_UpdateRect(display, 0, 0, 0, 0);
            }
        }
        ypos2 += vPixelWidth;
    }
  raus_rasterp:
    return;
}

void bitimage_graphics(int mode, int dotColumns) {
    switch (mode) {
    case 0:  // 60 x 60 dpi 9 needles
        hPixelWidth = printerdpih / (float) 60;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 1:  // 120 x 60 dpi 9 needles
        hPixelWidth = printerdpih / (float) 120;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 2:
        // 120 x 60 dpi 9 needles - not adjacent dot printing
        hPixelWidth = printerdpih / (float) 120;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 3:
        // 240 x 60 dpi 9 needles - not adjacent dot printing
        hPixelWidth = printerdpih / (float) 240;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 4:  // 80 x 60 dpi 9 needles
        hPixelWidth = printerdpih / (float) 80;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 5:  // 72 x 72 dpi 9 needles - unused in ESC/P2
        hPixelWidth = printerdpih / (float) 72;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 6:  // 90 x 60 dpi 9 needles
        hPixelWidth = printerdpih / (float) 90;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 7:  // 144 x 72 dpi 9 needles (ESC/P only)
        hPixelWidth = printerdpih / (float) 144;
        if (needles == 9) {
            vPixelWidth = printerdpiv / (float) 72;  // ESCP definition
        } else {
            vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition
        }
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 32:  // 60 x 180 dpi, 24 dots per column - row = 3 bytes
        hPixelWidth = printerdpih / (float) 60;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 33:  // 120 x 180 dpi, 24 dots per column - row = 3 bytes
        hPixelWidth = printerdpih / (float) 120;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 35:  // Resolution not verified possibly 240x216 sein
        hPixelWidth = printerdpih / (float) 240;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 38:  // 90 x 180 dpi, 24 dots per column - row = 3 bytes
        hPixelWidth = printerdpih / (float) 90;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 39:  // 180 x 180 dpi, 24 dots per column - row = 3 bytes
        hPixelWidth = printerdpih / (float) 180;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 40:  // 360 x 180 dpi, 24 dots per column - row = 3 bytes - not adjacent dot
        hPixelWidth = printerdpih / (float) 360;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 64:  // 60 x 60 dpi, 48 dots per column - row = 6 bytes
        hPixelWidth = printerdpih / (float) 60;
        vPixelWidth = printerdpiv / (float) 60;  // Das sind hier 1.2
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 65:  // 120 x 120 dpi, 48 dots per column - row = 6 bytes
        hPixelWidth = printerdpih / (float) 120;
        vPixelWidth = printerdpiv / (float) 120;  // Das sind hier 1.2
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 70:  // 90 x 180 dpi, 48 dots per column - row = 6 bytes
        hPixelWidth = printerdpih / (float) 90;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 71:  // 180 x 360 dpi, 48 dots per column - row = 6 bytes
        hPixelWidth = printerdpih / (float) 180;
        vPixelWidth = printerdpiv / (float) 360;  // Das sind hier 1.2
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 72:  // 360 x 360 dpi, 48 dots per column - row = 6 bytes - no adjacent dot printing
        hPixelWidth = printerdpih / (float) 360;
        vPixelWidth = printerdpiv / (float) 360;  // Das sind hier 1.2
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 73:  // 360 x 360 dpi, 48 dots per column - row = 6 bytes
        hPixelWidth = printerdpih / (float) 360;
        vPixelWidth = printerdpiv / (float) 360;  // Das sind hier 1.2
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    }
}

// Loads a font with 16bytes per char.
// Returns 1 if ok and -1 if not OK
int openfont(const char *filename)
{
    int rc = 1;
    FILE *font;

    font = fopen(filename, "r");

    if (font == NULL) {
        rc = -1;
    } else {
        if (fread(fontx, 1, FONT_SIZE, font) != FONT_SIZE) {
            rc = -1;
        }

        fclose(font);
    }

    return rc;
}

int direction_of_char = 1;
int printcharx(unsigned char chr)
{
    unsigned int adressOfChar = 0;
    unsigned int chr2;
    int i, fByte, extendedChar = 0;
    int boldoffset = 0;
    int boldoffset11= 0;
    int italiccount=0;
    unsigned char xd;
    float divisor=1;
    int yposoffset=0;
    int charHeight = 24; // 24 pin printer - characters 24 pixels high
    int fontDotWidth, fontDotHeight, character_spacing;

    chr2 = (unsigned int) chr;
    if (!use8bitchars && useItalicsCharSet && chr2 >= 160) {
        // In this case characters with ASCII 160-255 are treated as italic versions of the original
        extendedChar = 1;
        chr2 = chr2 - 128; // Normally upper character set - italic version of the lower character set
        chr = (unsigned char) chr2;
    }
    adressOfChar = chr2 << 4;  // Multiply with 16 to Get Adress
    hPixelWidth = printerdpih / (float) cdpih;
    vPixelWidth = printerdpiv / (float) cdpiv;
    character_spacing = 0;

    // Take into account the expected size of the font for a 24 pin printer:
    // Font size is 8 x 16:

    if (letterQuality == 1) {
        // LETTER QUALITY 360 x 144 dpi
        // -- uses (360 / cpi) x 24 pixel font - default is 10 cpi (36 dots), 12 cpi (10 dots), 15 cpi (24 dots)
        fontDotWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 24);
        fontDotHeight = (float) vPixelWidth * ((float) charHeight / (float) 16);
        if (chrSpacing > 0) character_spacing = printerdpih * ((float) chrSpacing / (float) 180);
    } else {
        // DRAFT QUALITY 120 x 144 dpi
        // -- uses (120 / cpi) x 24 pixel font - default is 10 cpi (12 dots), 12 cpi (10 dots), 15 cpi (8 dots)
        fontDotWidth = (float) hPixelWidth * (((float) 120 / (float) cpi) / (float) 8);
        fontDotHeight = (float) vPixelWidth * ((float) charHeight / (float) 16);
        if (chrSpacing > 0) character_spacing = printerdpih * ((float) chrSpacing / (float) 120);
    }

    // eigentlich sollte
    // das 16 sein da
    // jedes zeichen 16
    // Pixel gro?ist,
    // 18 sieht aber
    // besser aus
    boldoffset11=0;
    boldoffset=0;
    if (bold == 1) {
        boldoffset = (float) (hPixelWidth + vPixelWidth) / (float) 3;
        boldoffset11=boldoffset;
        //printf("%d\n", boldoffset);
    }
    if (italic==1 || extendedChar == 1) {
        italiccount=1;
    }
    test_for_new_paper();

    // SUPERSCRIPT / SUBSCRIPT 360 x 144 dpi
    // -- uses (360 / cpi) x 16 pixel font - default is 10 cpi (36 dots), 12 cpi (30 dots), 15 cpi (24 dots)
    // NOTE : if point size = 8, then subscript/superscript are also 8 point (8/72 inches)
    // character width for proportional fonts is different to full size proportional font.
    // Does not affect graphics characters
    // TO BE WRITTEN
    if (superscript==1) {
        if (multipoint_mode == 1) {
            // Use nearest to 2/3
            divisor=2.0/3.0;
            vPixelWidth=vPixelWidth*divisor;
            yposoffset=2;
        } else {
            fontDotHeight = vPixelWidth;
            yposoffset=2;
        }
    } else if (subscript==1) {
        if (multipoint_mode == 1) {
            // Use nearest to 2/3
            divisor=2.0/3.0;
            vPixelWidth=vPixelWidth*divisor;
            yposoffset=26;
        } else {
            fontDotHeight = vPixelWidth;
            yposoffset=26;
        }
    }

    hPixelWidth = fontDotWidth;
    vPixelWidth = fontDotHeight;

    if (double_width || double_width_single_line) {
        hPixelWidth *= 2;
        character_spacing *= 2;
    }
    if (double_height == 1) {
        // If ESC w sent on first line of page does NOT affect the first line
        // Move ypos back up page to allow base line of character to remain the same
        if ((chr!=32) && (ypos >= charHeight * vPixelWidth)) {
            yposoffset -= charHeight * vPixelWidth; // Height of one character at double height = 2 x 24
            vPixelWidth *= 2;
        }
    }
    if (quad_height == 1) {
        // Star NL-10 ENLARGE command - does NOT affect the first line
        // Move ypos back up page to allow base line of character to remain the same
        if ((chr!=32) && (ypos >= charHeight * 3 * vPixelWidth)) {
            yposoffset -= charHeight * 3 * vPixelWidth; // Height of one character at quad height = 4 x 24
            vPixelWidth *= 4;
        }
    }

    if (direction_of_char == 1) {
        for (i = 0; i <= 15; i++) {
            xd = fontx[adressOfChar + i];
            // TO BE UPDATED as Underlining etc covers spaces, and non-graphics characters
            if ((underlined>0) || (strikethrough>0) || (overscore>0)) {
                if ((i==14) && ((underlined==1) || (underlined==3)) ) xd=255;
                if ((i==13) && ((underlined==2) || (underlined==4)) ) xd=255;
                if ((i==15) && ((underlined==2) || (underlined==4)) ) xd=255;

                if ((i==8 ) && ((strikethrough==1) || (strikethrough==3)) ) xd=255;
                if ((i==7 ) && ((strikethrough==2) || (strikethrough==4)) ) xd=255;
                if ((i==9 ) && ((strikethrough==2) || (strikethrough==4)) ) xd=255;

                if ((i==1 ) && ((overscore==1) || (overscore==3)) ) xd=255;
                if ((i==0 ) && ((overscore==2) || (overscore==4)) ) xd=255;
                if ((i==3 ) && ((overscore==2) || (overscore==4)) ) xd=255;
            }

            for (fByte = xpos + italiccount * (7-i); fByte < xpos + 8 * hPixelWidth + italiccount * (7-i); fByte+= hPixelWidth) {
                if (xd & 128) putpixelbig(fByte, ypos + yposoffset + i * vPixelWidth,
                                hPixelWidth + boldoffset, vPixelWidth + boldoffset11);
                xd = xd << 1;
            }
        }
    } else {
        for (i = 0; i <= 15; i++) {
            xd = fontx[adressOfChar + i];
            // TO BE UPDATED as Underlining etc covers spaces, and non-graphics characters
            if ((underlined>0) || (strikethrough>0) || (overscore>0)) {
                if ((i==14) && ((underlined==1) || (underlined==3)) ) xd=255;
                if ((i==13) && ((underlined==2) || (underlined==4)) ) xd=255;
                if ((i==15) && ((underlined==2) || (underlined==4)) ) xd=255;

                if ((i==8 ) && ((strikethrough==1) || (strikethrough==3)) ) xd=255;
                if ((i==7 ) && ((strikethrough==2) || (strikethrough==4)) ) xd=255;
                if ((i==9 ) && ((strikethrough==2) || (strikethrough==4)) ) xd=255;

                if ((i==1 ) && ((overscore==1) || (overscore==3)) ) xd=255;
                if ((i==0 ) && ((overscore==2) || (overscore==4)) ) xd=255;
                if ((i==3 ) && ((overscore==2) || (overscore==4)) ) xd=255;
            }
            for (fByte = xpos + italiccount * (7-i); fByte < xpos + 8 * hPixelWidth + italiccount * (7-i); fByte+= hPixelWidth) {
                if (xd & 001) putpixelbig(fByte,ypos + yposoffset+ i * vPixelWidth,
                                hPixelWidth + boldoffset, vPixelWidth + boldoffset11);
                xd = xd >> 1;
            }
        }
    }
    // Add the actual character width
    xpos += hPixelWidth * 8;
    // Add any character spacing - taking account of any continuous line scoring of printing
    if (character_spacing>0) {
        if ((underlined==1) || (underlined==2) || (strikethrough==1) || (strikethrough==2) || (overscore==1) || (overscore==2)) {
            for (i = 0; i <= 15; i++) {
                xd = 0;
                if ((i==14) && (underlined==1) ) xd=255;
                if ((i==13) && (underlined==2) ) xd=255;
                if ((i==15) && (underlined==2) ) xd=255;

                if ((i==8 ) && (strikethrough==1) ) xd=255;
                if ((i==7 ) && (strikethrough==2) ) xd=255;
                if ((i==9 ) && (strikethrough==2) ) xd=255;

                if ((i==1 ) && (overscore==1) ) xd=255;
                if ((i==0 ) && (overscore==2) ) xd=255;
                if ((i==3 ) && (overscore==2) ) xd=255;

                if (xd) {
                    putpixelbig(xpos, ypos + yposoffset + i * vPixelWidth,
                                    character_spacing + boldoffset, vPixelWidth + boldoffset11);
                }
            }

        } else {
            xpos += character_spacing;
        }
    }
}

int print_space(int showUnderline)
{
    int i, fByte;
    unsigned char xd;
    int yposoffset=0;
    int charHeight = 24; // 24 pin printer - characters 24 pixels high
    float fontDotWidth, fontDotHeight, character_spacing;
    int boldoffset = 0;
    int boldoffset11= 0;

    hPixelWidth = printerdpih / (float) cdpih;
    vPixelWidth = printerdpiv / (float) cdpiv;
    character_spacing = 0;

    // Take into account the expected size of the font for a 24 pin printer:
    // Font size is 8 x 16:
    boldoffset11=0;
    boldoffset=0;
    if (bold == 1) {
        boldoffset = (float) (hPixelWidth + vPixelWidth) / (float) 3;
        boldoffset11=boldoffset;
        //printf("%d\n", boldoffset);
    }
    if (letterQuality == 1) {
        // LETTER QUALITY 360 x 144 dpi
        // -- uses (360 / cpi) x 24 pixel font - default is 10 cpi (36 dots), 12 cpi (10 dots), 15 cpi (24 dots)
        fontDotWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 24);
        fontDotHeight = (float) vPixelWidth * (float) charHeight / (float) 16;
        if (chrSpacing > 0) character_spacing = printerdpih * ((float) chrSpacing / (float) 180);
    } else {
        // DRAFT QUALITY 120 x 144 dpi
        // -- uses (120 / cpi) x 24 pixel font - default is 10 cpi (12 dots), 12 cpi (10 dots), 15 cpi (8 dots)
        fontDotWidth = (float) hPixelWidth * (((float) 120 / (float) cpi) / (float) 8);
        fontDotHeight = (float) vPixelWidth * (float) charHeight / (float) 16;
        if (chrSpacing > 0) character_spacing = printerdpih * ((float) chrSpacing / (float) 180);
    }

    test_for_new_paper();

    hPixelWidth = fontDotWidth;
    vPixelWidth = fontDotHeight;

    if ((double_width == 1) || (double_width_single_line == 1)) {
        hPixelWidth = hPixelWidth * 2;
        character_spacing = character_spacing * 2;
    }

    // Because we are only printing spaces - we can ignore it if there is no underlining
    if ((showUnderline==1) && ((underlined>0) || (strikethrough>0) || (overscore>0))) {
        for (i = 0; i <= 15; i++) {
            xd = 0;  // Nominally just space
            if ((i==14) && ((underlined==1) || (underlined==3)) ) xd=255;
            if ((i==13) && ((underlined==2) || (underlined==4)) ) xd=255;
            if ((i==15) && ((underlined==2) || (underlined==4)) ) xd=255;

            if ((i==8 ) && ((strikethrough==1) || (strikethrough==3)) ) xd=255;
            if ((i==7 ) && ((strikethrough==2) || (strikethrough==4)) ) xd=255;
            if ((i==9 ) && ((strikethrough==2) || (strikethrough==4)) ) xd=255;

            if ((i==1 ) && ((overscore==1) || (overscore==3)) ) xd=255;
            if ((i==0 ) && ((overscore==2) || (overscore==4)) ) xd=255;
            if ((i==3 ) && ((overscore==2) || (overscore==4)) ) xd=255;

            if (xd > 0) {
                putpixelbig(xpos + fByte * hPixelWidth, ypos + yposoffset + i * vPixelWidth,
                                hPixelWidth * 8 + boldoffset, vPixelWidth + boldoffset11);
            }
        }
    }

    // Add the actual character width
    xpos += hPixelWidth * 8;
    // Add any character spacing - taking account of any continuous line scoring of printing
    if (character_spacing>0) {
        if ((underlined==1) || (underlined==2) || (strikethrough==1) || (strikethrough==2) || (overscore==1) || (overscore==2)) {
            for (i = 0; i <= 15; i++) {
                xd = 0;
                if ((i==14) && (underlined==1) ) xd=255;
                if ((i==13) && (underlined==2) ) xd=255;
                if ((i==15) && (underlined==2) ) xd=255;

                if ((i==8 ) && (strikethrough==1) ) xd=255;
                if ((i==7 ) && (strikethrough==2) ) xd=255;
                if ((i==9 ) && (strikethrough==2) ) xd=255;

                if ((i==1 ) && (overscore==1) ) xd=255;
                if ((i==0 ) && (overscore==2) ) xd=255;
                if ((i==3 ) && (overscore==2) ) xd=255;

                if (xd) {
                    putpixelbig(xpos, ypos + yposoffset + i * vPixelWidth,
                                    character_spacing + boldoffset, vPixelWidth + boldoffset11);
                }
            }

        } else {
            xpos += character_spacing;
        }
    }
}

void print_character(unsigned char xChar)
{
    printcharx(xChar);
    countcharz++;
    if (countcharz > 20) {
        if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
        countcharz = 0;
    }
    // If out of paper area on the right side, do a newline and shift
    // printer head to the left
    if (xpos > (marginrightp - hPixelWidth * 8)) {
        xpos = marginleftp;
        ypos += line_spacing;
    }
}

void cpulimit()
{
    char y[1000];
    return;
    sprintf(y, "cpulimit -p%d  -l 90 &", getpid());
    system(y);
}

void makeallpaths()
{
    char x[1000];

    sprintf(x,"mkdir %s 2>>/dev/null",pathraw);
    system(x);
    sprintf(x,"mkdir %s 2>>/dev/null",pathpng);
    system(x);
    sprintf(x,"mkdir %s 2>>/dev/null",pathpdf);
    system(x);
    sprintf(x,"mkdir %s 2>>/dev/null",patheps);
    system(x);
}

char* readFileParameter(const char* filename, char* dest, size_t maxlen)
{
    FILE *FL;
    FL = fopen(filename, "r");
    if (FL != NULL) {
        fgets(dest, maxlen - 1, FL);
        if (!quiet_mode) printf("%s parameter = %s \n", filename, dest);
        fclose(FL);
    }
    return dest;
}

int convertUnixWinMac(char * source,char * destination)
{
    FILE * sourceFile, * destinationFile;

    int bytex,bytex1=0,bytex2=0;

    sourceFile      = fopen(source,"r");
    destinationFile = fopen(destination,"w");

    if (NULL==sourceFile) {
      fprintf(stderr, "Could not open raw file for reading ---> %s\n",source);
      return -1;
    }
    if (NULL==destinationFile){
      fprintf(stderr, "Could not open raw file for writing ---> %s\n",destination);
      return -1;
    }
    bytex=fgetc(sourceFile);
    while (bytex!=EOF) {
        if (outputFormatText==1) {
            //unix format
            if ((bytex1==10) && (bytex==10)) {
               bytex1=0;
               goto nowrite;
            } else if ((bytex1==10) && (bytex!=13)) {
               bytex1=0;
            } else if (bytex==13) {
               bytex=10;
               bytex1=10;
            }
        } else if (outputFormatText==2) {
            //windows
            if ((bytex==10) && (bytex1!=13)) {
                //schreibe 0d 0a
                bytex=13;
                bytex2=10;
                bytex1=0;
                goto fini;
            } else if ((bytex==10) && (bytex1==13)) {
                //alles OK
                bytex1=0;
                goto nowrite;
            } else if ((bytex1==13) && (bytex!=10)) {
                bytex1=0;
            } else if ((bytex1==13) && (bytex==10)) {
                bytex1=0;
                goto nowrite;
            } else if (bytex==13) {
                bytex2=10;
                bytex1=13;
            }
        } else if (outputFormatText==3) {
            //mac
            if ((bytex1!=13) && (bytex==10)) {
               bytex=13;
               bytex1=0;
               goto fini;
            } else if ((bytex1==13) && (bytex==10)) {
               bytex1=0;
               goto nowrite;
            } else if ((bytex1==13) && (bytex!=13)) {
               bytex1=0;
               goto fini;
            } else if (bytex==13) {
                bytex1=13;
            }
        }
fini:
        fputc(bytex,destinationFile);
        if (bytex2!=0) {
            fputc(bytex2,destinationFile);
            bytex2=0;
        }

nowrite:
        bytex=fgetc(sourceFile);
    }
    fclose(destinationFile);
    fclose(sourceFile);
}

void set_page_size(double width, double height)
{
    // Set page size.
    // Based it on support for 720dpi (24 pin printers)
    // All settings have to be in inches - 1 mm = (1/25.4)"
    pageSetWidth  = (int)(printerdpih * (width  / 25.4));
    pageSetHeight = (int)(printerdpiv * (height / 25.4));
}

void set_page_margin(double left, double right, double top, double bottom)
{
    // Set margins. Page size has to be set first!
    // Based it on support for 720dpi (24 pin printers)
    // All settings have to be in inches - 1 mm = (1/25.4)"

    defaultMarginLeftp   = (int)(printerdpih * (left / 25.4));
    defaultMarginRightp  = pageSetWidth - (int)(printerdpih * (right / 25.4));
    defaultMarginTopp    = (int)(printerdpiv * (top / 25.4));
    defaultMarginBottomp = pageSetHeight - (int)(printerdpiv * (bottom / 25.4));
}

void usage(const char* progname)
{
    printf("Usage: %s -o PATH -f FONT [OPTIONS] [FILE]\n", progname);
    printf("       %s -h\n", progname);
    printf("       %s -v\n", progname);

    puts("\nMandatory arguments:");
    puts("  -o PATH              Where to store the output files.");
    puts("  -f FONT              Use the specified font file.");

    puts("\nOPTIONS:");
    puts("  -d DIR               Set font direction:");
    puts("                         left");
    puts("                         right (default)");
    puts("  -s [NUM]             Display printout in sdl window.");
    puts("                       Optional numeric argument reduces sdl display to that");
    puts("                       percentage of original size.");
    puts("  -p NUM               Select predifined page size:");
    puts("                         0: A4 portrait (210 x 297 mm) (default)");
    puts("                         1: A4 landscape (297 x 210 mm)");
    puts("                         2: 12\" paper - US Single Sheet (8 x 12\")");
    puts("  or:");
    puts("  -p W,H               Select custom page size in millimeters:");
    puts("                         W: width");
    puts("                         H: height");
    puts("  -m [L],[R],[T],[B]   Set page margins in millimeters.");
    puts("                       You can leave fields blank to keep the default values:");
    puts("                         L (left):    3.0 mm (paper size 2:  2.5 mm)");
    puts("                         R (right):   3.0 mm (paper size 2:  2.5 mm)");
    puts("                         T (top):     8.5 mm (paper size 2: 10.0 mm)");
    puts("                         B (bottom): 13.5 mm (paper size 2: 10.0 mm)");
    puts("  or:");
    puts("  -m V,H               Set page margins in millimeters:");
    puts("                         V: vertical sides (left and right)");
    puts("                         H: horizontal sides (top and bottom)");
    puts("  or:");
    puts("  -m A                 Set page margins in millimeters for all sides.");
    puts("  -l STR               Sets linefeed endings:");
    puts("                         none:    No conversion (default)");
    puts("                         unix:    Unix (LF).");
    puts("                         windows: Windows (CR+LF)");
    puts("                         mac:     MAC (CR)");
    puts("  -8                   Use 8 bit characters.");
    puts("  -9                   Set 9 pin printer mode.");
    puts("  -i                   Use character codes 160-255 as italics.");
    puts("  -q                   Be quiet, don't write any messages.");

    puts("\nInformational arguments:");
    puts("  -h                   Display this help and exit.");
    puts("  -v                   Display version information and exit.");

    puts("\nThe options '-8' and '-i' cannot be combined.");

    puts("\nIf no file is specified, input will be read from stdin.");

    puts("");
}

void usage_hint(const char* progname)
{
    fprintf(stderr, "Try '%s -h' for more information.\n", progname);
}

int main(int argc, char *argv[])
{
    unsigned char xd = 0;
    int xposold;
    int i, j;
    int opt;
    const char* fontfile = NULL;
    const char* input_filename = NULL;
    FILE *FL;
    double sizeh, sizev;
    double leftm, rightm, topm, bottomm;

    if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[1], "-v") == 0) {
            printf("%s %s\n", basename(argv[0]), version);
            return 0;
        }
    }

    cpulimit();

    // get default from /root/config/linefeed_ending
    if (cfileexists(LINEFEED_CONFIG)) {
        char buffer[256];
        readFileParameter(LINEFEED_CONFIG, buffer, sizeof(buffer) - 1);
        if (strcasecmp(buffer,"unix") == 0) {
            outputFormatText=1;
        } else if (strncasecmp(buffer,"windows", 7) == 0) {
            outputFormatText=2;
        } else if (strncasecmp(buffer,"mac", 3) == 0) {
            outputFormatText=3;
        }
    }

    path[0] = '\0';

    // get default from /root/config/output_path
    if (cfileexists(PATH_CONFIG)) {
        FL=fopen(PATH_CONFIG, "r");
        fscanf(FL, "%s", path);
        fclose(FL);
    }

    sizeh   = 210.0;
    sizev   = 297.0;
    leftm   =   3.0;
    rightm  =   3.0;
    topm    =   8.5;
    bottomm =  13.5;

    // Parse command line arguments

    int margin_set = 0;

    do {
        opt = getopt(argc, argv, "f:d:s::o:p:m:l:89iq");

        switch (opt) {
        
            case '9':
        	needles = 9;
        	break;    	
        
            case 'f':
                fontfile = optarg;
                break;

            case 'd':
                if (strcasecmp(optarg, "left") == 0) {
                    direction_of_char = 0;
                } else if (strcasecmp(optarg, "right") == 0) {
                    direction_of_char = 1;
                } else {
                    fprintf(stderr, "%s: invalid font direction: '%s'\n", argv[0], optarg);
                    return 2;
                }

                break;

            case 's':
                sdlon = 1;

                if (optarg != NULL) {
                    int err = 0;
                    char* endptr;

                    divi = strtof(optarg, &endptr);

                    if (errno == ERANGE || *endptr != '\0') {
                        err = 1;
                    } else if (divi <= 0.0 || divi > 100.0) {
                        err = 1;
                    } else {
                        divi = 100.0 / divi;
                    }

                    if (err) {
                        fprintf(stderr, "%s: invalid percentage '%s'\n", argv[0], optarg);
                        return 2;
                    }
                }

                break;

            case 'o':
                strcpy(path, optarg);
                break;

            case 'p':
                if (strcmp(optarg, "0") == 0) {
                    sizeh = 210.0;
                    sizev = 297.0;

                    if (!margin_set) {
                        leftm   =  3.0;
                        rightm  =  3.0;
                        topm    =  8.5;
                        bottomm = 13.5;
                    }
                } else if (strcmp(optarg, "1") == 0) {
                    sizeh = 297.0;
                    sizev = 210.0;

                    if (!margin_set) {
                        leftm   =  3.0;
                        rightm  =  3.0;
                        topm    =  8.5;
                        bottomm = 13.5;
                    }
                } else if (strcmp(optarg, "2") == 0) {
                    sizeh = 205.0;
                    sizev = 304.8;

                    if (!margin_set) {
                        leftm   =  2.5;
                        rightm  =  2.5;
                        topm    = 10.0;
                        bottomm = 10.0;
                    }
                } else {
                    char* pos_comma = strchr(optarg, ',');
                    int err = 0;

                    if (pos_comma == NULL) {
                        err = 1;
                    } else {
                        double width, height;
                        char* endptr;

                        *pos_comma = '\0';
                        width = strtod(optarg, &endptr);

                        if (errno == ERANGE || *endptr != '\0') {
                            err = 1;
                        }

                        *pos_comma = ',';

                        if (!err) {
                            height = strtod(pos_comma + 1, &endptr);

                            if (errno == ERANGE || *endptr != '\0') {
                                err = 1;
                            } else {
                                sizeh = width;
                                sizev = height;
                            }
                        }
                    }

                    if (err) {
                        fprintf(stderr, "%s: invalid page size '%s'\n", argv[0], optarg);
                        return 2;
                    }
                }

                break;

            case 'm':
                {
                    double margin[4] = { 3.0, 3.0, 8.5, 13.5 };
                    char*  start[5]  = { optarg, NULL, NULL, NULL, NULL };

                    double *left = NULL, *right = NULL, *top = NULL, *bottom = NULL;
                    char* endptr;
                    int err = 0;

                    for (int i = 1; i < sizeof(start) / sizeof(start[0]); ++i) {
                        start[i] = strchr(start[i - 1], ',');

                        if (start[i] == NULL) {
                            break;
                        } else {
                            ++start[i];
                        }
                    }

                    if (start[1] == NULL) {
                        // Only one margin specified -> set for all sides
                        left   = &margin[0];
                        right  = &margin[0];
                        top    = &margin[0];
                        bottom = &margin[0];
                    } else if (start[2] == NULL) {
                        // Two values specified -> first one for vertical sides and second one for horizontal sides
                        left   = &margin[0];
                        right  = &margin[0];
                        top    = &margin[1];
                        bottom = &margin[1];
                    } else if (start[3] != NULL && start[4] == NULL) {
                        // Four values specified
                        left   = &margin[0];
                        right  = &margin[1];
                        top    = &margin[2];
                        bottom = &margin[3];
                    } else {
                        // Three or more than four values specified
                        err = 1;
                    }

                    for (int i = 0; i < 4 && !err; ++i) {
                        if (start[i] == NULL) {
                            break;
                        } else if (*start[i] != '\0') {
                            char* endptr;

                            if (start[i + 1] != NULL) {
                                start[i + 1][-1] = '\0';
                            }

                            if (*start[i] != '\0') {
                                margin[i] = strtod(start[i], &endptr);

                                if (errno == ERANGE || *endptr != '\0') {
                                    err = 1;
                                }
                            }

                            if (start[i + 1] != NULL) {
                                start[i + 1][-1] = ',';
                            }
                        }
                    }

                    if (err) {
                        fprintf(stderr, "%s: invalid margin: '%s'\n", argv[0], optarg);
                        return 2;
                    } else {
                        leftm      = *left;
                        rightm     = *right;
                        topm       = *top;
                        bottomm    = *bottom;
                        margin_set = 1;
                    }
                }

                break;

            case 'l':
                if (strcasecmp(optarg, "unix") == 0) {
                    outputFormatText=1;
                } else if (strcasecmp(optarg, "windows") == 0) {
                    outputFormatText=2;
                } else if (strcasecmp(optarg, "mac") == 0) {
                    outputFormatText=3;
                } else {
                    fprintf(stderr, "%s: invalid linefeed ending: '%s'\n", argv[0], optarg);
                    return 2;
                }

                break;

            case '8':
                if (useItalicsCharSet) {
                    fprintf(stderr, "%s: options '-8' and '-i' cannot be combined\n", argv[0]);
                    usage_hint(argv[0]);
                    return 2;
                } else {
                    use8bitchars = 1;
                }

                break;

            case 'i':
                if (use8bitchars) {
                    fprintf(stderr, "%s: options '-8' and '-i' cannot be combined.\n", argv[0]);
                    usage_hint(argv[0]);
                    return 2;
                } else {
                    useItalicsCharSet = 1;
                }

                break;

            case 'q':
                quiet_mode = 1;
                break;

            case ':':
            case '?':
                usage_hint(argv[0]);
                return 2;

            default:
                break;

        }
    } while (opt >= 0);

    if (fontfile == NULL) {
        fprintf(stderr, "%s: no font specified.\n", argv[0]);
        usage_hint(argv[0]);
        return 2;
    }

    if (path[0] == '\0') {
        fprintf(stderr, "%s: no output path specified.\n", argv[0]);
        usage_hint(argv[0]);
        return 2;
    }

    if (optind < argc) {
        if (optind + 1 < argc) {
            fprintf(stderr, "%s: too many files specified.\n", argv[0]);
            usage_hint(argv[0]);
            return 2;
        } else {
            input_filename = argv[optind];
        }
    } else {
        input_filename = "/dev/stdin";
    }

    set_page_size(sizeh, sizev);
    set_page_margin(leftm, rightm, topm, bottomm);

    switch (outputFormatText) {
        case 1:
            puts("Unix-mode on (LF).");
            break;

        case 2:
            puts("Windows-mode on (CR+LF).");
            break;

        case 3:
            puts("MAC-mode on (CR).");
            break;

        default:
            break;

    }

    if (!quiet_mode) {
        printf("\ndelays around ack: t1=%d    t2=%d    t3=%d    t4=%d    t5=%d\n",t1,t2,t3,t4,t5);
    }

    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }
    strcpy(pathraw, path);
    strcpy(pathpng, path);
    strcpy(patheps, path);
    strcpy(pathpdf, path);

    strcat(pathraw,   "");
    strcat(pathpng,   "png/");
    strcat(pathpdf,   "pdf/");
    strcat(patheps,   "eps/");

    makeallpaths();

    page = dirX(pathraw);
    reduce_pages(page, pathraw);
    page = dirX(pathpng);
    reduce_pages(page, pathpng);

    page = dirX(pathpdf);
    reduce_pages(page, pathpdf);
    page = dirX(pathpdf) + 1;

    // goto raus;
    initialize(input_filename);

    openfont(fontfile);

    if (sdlon) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fputs("Error on SDL init\n", stderr);
            exit(1);
        }
    }

    // Set the video mode
    xdim = pageSetWidth / divi;
    ydim = pageSetHeight / divi;
    if (sdlon) {
        display = SDL_SetVideoMode(xdim, ydim, 24, SDL_HWSURFACE);
        if (display == NULL) {
            fputs("Error on SDL display mode setting\n", stderr);
            exit(1);
        }
        // Set the title bar
        SDL_WM_SetCaption("Printer Output", "Printer Output");

        // SDL_UpdateRect (display, 0, 0, 0, 0);
        erasesdl();
        SDL_UpdateRect(display, 0, 0, 0, 0);
    }

    // Do we support 8 Bit printing - in which case, upper control codes are ignored!
    if (use8bitchars) print_uppercontrolcodes = 1;

main_loop_for_printing:
    xpos = marginleftp;
    ypos = margintopp;
    if (sdlon) erasesdl();
    erasepage();
    // Clear tab marks
    for (i = 0; i < 32; i++) hTabulators[i] = (printerdpih / cpi) * (8*i); // Default is every 8 characters
    for (i = 0; i < (16 * 8); i++) vTabulators[i] = 0;

    i = 0;
    xd = 0;
    state = 1;
    // ASCII Branch
    while (state != 0) {
        // read next char
        state = read_byte_from_file((char *) &xd);
        if (state == 0) {
            break;
        }
        fflush(stdout);
        i++;
        if (graphics_mode ==1) {
            // Epson ESC/P2 graphics mode - limited commands:
            switch (xd) {
            case 10:    // lf (0x0a)
                ypos += line_spacing;
                xpos = marginleftp;
                double_width_single_line = 0;
                test_for_new_paper();
                break;
            case 12:    // form feed (neues blatt)
                ypos = pageSetHeight + 1;  // just put it in an out of area position
                test_for_new_paper();
                i = 0;
                double_width_single_line = 0;
                break;
            case 13:    // cr (0x0d)
                xpos = marginleftp;
                if (auto_LF) {
                    ypos += line_spacing;
                    double_width_single_line = 0;
                    test_for_new_paper();
                }
                break;
            case 27:    // ESC Escape (do nothing, will be processed later in this code)
                break;
            }
            // ESc Branch for graphics mode
            if (xd == (int) 27) {   // ESC v 27 v 1b
                state = read_byte_from_file((char *) &xd);

                switch (xd) {
                case '@':    // ESC @ Initialize
                    cpi                    =  10;
                    pitch                  =  10;
                    multipoint_mode        =   0;
                    hmi                    = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    line_spacing           = printerdpiv * ((float) 1 / (float) 6); // normally 1/6 inch line spacing
                    chrSpacing             =   0;
                    dpih                   = 240;
                    dpiv                   = 216;
                    letterQuality          =   0;
                    proportionalSpacing    =   0;
                    printColour            =   0;
                    bold                   =   0;
                    italic                 =   0;
                    underlined             =   0;
                    superscript            =   0;
                    subscript              =   0;
                    strikethrough          =   0;
                    overscore              =   0;
                    double_width           =   0;
                    double_width_single_line = 0;
                    double_height          =   0;
                    quad_height            =   0;
                    outline_printing       =   0;
                    shadow_printing        =   0;
                    print_controlcodes     =   0;
                    print_uppercontrolcodes =  0;
                    if (use8bitchars) print_uppercontrolcodes = 1;
                    graphics_mode          =   0;
                    microweave_printing    =   0;
                    vTabulatorsSet         =   0;
                    vTabulatorsCancelled   =   0;
                    defaultUnit            =   0;
                    pageManagementUnit     =   0;
                    relHorizontalUnit      =   0;
                    absHorizontalUnit      =   0;
                    relVerticalUnit        =   0;
                    absVerticalUnit        =   0;
                    break;
                case '.':
                    // Raster printing ESC . c v h m nL nH d1 d2 . . . dk print bit-image graphics.
                    state = read_byte_from_file((char *) &c);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &v);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &h);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &m);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;

                    switch (v) {
                        case 5:
                            vPixelWidth = printerdpiv / (float) 720;
                            break;
                        case 10:
                            vPixelWidth = printerdpiv / (float) 360;
                            break;
                        case 20:
                            vPixelWidth = printerdpiv / (float) 180;
                            break;
                    }
                    switch (h) {
                        case 5:
                            hPixelWidth = printerdpih / (float) 720;
                            break;
                        case 10:
                            hPixelWidth = printerdpih / (float) 360;
                            break;
                        case 20:
                            hPixelWidth = printerdpih / (float) 180;
                            break;
                    }
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    if ( microweave_printing == 1 ) m = 1;
                    switch (c) {
                    case 0:
                        // Normal graphics - non-compressed
                        _line_raster_print(m, dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                        break;
                    case 1:
                        _line_raster_print(m, dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
                        break;
                    case 2:
                        // TIFF compressed mode
                        _tiff_delta_printing(2, hPixelWidth, vPixelWidth);
                        break;
                    case 3:
                        // Delta Row compressed mode
                        _tiff_delta_printing(3, hPixelWidth, vPixelWidth);
                        break;
                    }
                    break;
                case '+':    // Set n/360-inch line spacing ESC + n
                    state = read_byte_from_file((char *) &xd);
                    line_spacing = printerdpiv * ((float) xd / (float) 360);
                    break;
                case '(':
                    state = read_byte_from_file((char *) &nL);
                    switch (nL) {
                    case 'C':
                        // 27 40 67 nL nH mL mH Set page length in defined unit
                        // page size not implemented yet
                        state = read_byte_from_file((char *) &nL); // always 2
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // always 0
                        if (state == 0) break;
                        if (nL != 4) {
                            // Original ESC/P2 standard
                            state = read_byte_from_file((char *) &mL);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mH);
                            if (useExtendedSettings) {
                                thisDefaultUnit = pageManagementUnit;
                            } else {
                                thisDefaultUnit = defaultUnit;
                            }
                            if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                            /* TO DO
                            int pageLength = ((mH * 256) + mL) * (int) thisDefaultUnit;
                            if (pageLength > pageSetHeight) {
                                // Free more memory and reset the pagesetheight and default margins
                            }
                            */
                        } else if (nL == 4) {
                            // Extended standard
                            state = read_byte_from_file((char *) &m1);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m2);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m3);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m4);
                            if (state == 0) break;
                            thisDefaultUnit = pageManagementUnit;
                            if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                            /* TO DO
                            int pageLength = ((m4 * 256 * 256 * 256) + (m3 * 256 *256) + (m2 * 256) + m1) * (int) thisDefaultUnit;
                            if (pageLength > pageSetHeight) {
                                // Free more memory and reset the pagesetheight and default margins
                            }
                            */
                        }
                        margintopp = defaultMarginTopp;
                        marginbottomp = defaultMarginBottomp;
                        ypos = margintopp;
                        break;
                    case 'c':
                        // 27 40 67 nL nH tL tH bL bH Set page format
                        state = read_byte_from_file((char *) &nL); // always 4
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // always 0
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &tL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &tH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &bL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &bH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        margintopp = ((tH * 256) + tL) * (int) thisDefaultUnit;
                        marginbottomp = ((bH * 256) + bL) * (int) thisDefaultUnit;
                        if (marginbottomp > 22 * printerdpih) marginbottomp = 22 * printerdpih; // Max 22 inches
                        ypos = margintopp;
                        // cancel top and bottom margins (margintopp and marginbottomp - to be implemented)
                        break;
                    case 'V':
                        // 27 40 86 nL nH mL mH Set absolute vertical print position
                        state = read_byte_from_file((char *) &nL); // Always 2
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Always 0
                        if (state == 0) break;
                        if (nL != 4) {
                            // Original ESC/P2 standard
                            state = read_byte_from_file((char *) &mL);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mH);
                            if (useExtendedSettings) {
                                thisDefaultUnit = absVerticalUnit;
                                if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                                ypos2 = margintopp + ((mH * 256) + mL) * (int) thisDefaultUnit;
                            } else {
                                thisDefaultUnit = defaultUnit;
                                if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                                ypos2 = margintopp + ((mH * 256) + mL) * (int) thisDefaultUnit;
                            }
                            // Ignore if movement is more than 179/360" upwards
                            if (ypos2 < (ypos - (printerdpiv * ((float) 179/(float) 360))) ) ypos2 = ypos;
                            // Ignore if command would move upwards after graphics command sent on current line, or above where graphics have
                            // previously been printed - to be implemented
                        } else if (nL == 4) {
                            // Extended standard
                            state = read_byte_from_file((char *) &m1);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m2);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m3);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m4);
                            if (state == 0) break;
                            thisDefaultUnit = absVerticalUnit;
                            if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                            ypos2 = margintopp + ((m4 * 256 * 256 * 256) + (m3 * 256 *256) + (m2 * 256) + m1) * (int) thisDefaultUnit;
                            // Ignore if movement is negative
                            if (ypos2 < ypos) ypos2 = ypos;
                        }
                        ypos = ypos2;
                        test_for_new_paper();
                        break;
                    case 'v':
                        // 27 40 118 nL nH mL mH Set relative vertical print position
                        state = read_byte_from_file((char *) &nL); // Always 2
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Always 0
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &mL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &mH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        advance = mH * 256 + mL;
                        if (advance >= 32768) {
                            // Handle negative movement mH > 127
                            advance = (128 * 256) - advance;
                        }
                        ypos2 = ypos + advance * (int) thisDefaultUnit; 
                        // Ignore if movement is more than 179/360" upwards
                        if (ypos2 < (ypos - (printerdpiv * ((float) 179/(float) 360))) ) ypos2 = ypos;
                        // ignore if command would move upwards after graphics command sent on current line, or above where graphics have
                        // previously been printed - to be written
                        if (ypos2 < margintopp) {
                            // No action
                        } else {
                            ypos = ypos2;
                            test_for_new_paper();
                        }
                        break;
                    case 'U':
                        // 27 40 85 nL nH m Set unit
                        state = read_byte_from_file((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Always 0
                        if (state == 0) break;
                        if (nL != 5) {
                            // Original ESC/P2 standard
                            int useExtendedSettings = 0;
                            state = read_byte_from_file((char *) &m);
                            defaultUnit = ((float) m / (float) 3600) * printerdpiv; // set default unit to m/3600 inches
                            // extended standard
                            pageManagementUnit = defaultUnit;
                            relHorizontalUnit = defaultUnit;
                            absHorizontalUnit = defaultUnit;
                            relVerticalUnit = defaultUnit;
                            absVerticalUnit = defaultUnit;
                        } else if (nL == 5) {
                            // Extended standard
                            int useExtendedSettings = 1;
                            state = read_byte_from_file((char *) &m1); // P
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m2); //  V
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m3); // H
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mL);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mH);
                            if (state == 0) break;
                            pageManagementUnit = ((float) m1 / (float) ((mH * 256) + mL) * printerdpiv);
                            relVerticalUnit = ((float) m2 / (float) ((mH * 256) + mL) * printerdpiv);
                            absVerticalUnit = ((float) m2 / (float) ((mH * 256) + mL) * printerdpiv);
                            relHorizontalUnit = ((float) m3 / (float) ((mH * 256) + mL) * printerdpiv);
                            absHorizontalUnit = ((float) m3 / (float) ((mH * 256) + mL) * printerdpiv);
                        }
                        break;
                    case 'i':
                        // ESC ( i 01 00 n  Select microweave print mode
                        state = read_byte_from_file((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m);
                        if ((nL == 1) && (nH == 0) ) {
                            if ((m == 0) || (m == 48)) microweave_printing = 0;
                            if ((m == 1) || (m == 49)) microweave_printing = 1;
                        }
                        break;
                    case '$':
                        // ESC ( $ 04 00 m1 m2 m3 m4 Set absolute horizontal print position
                        // Only effective in graphics mode
                        state = read_byte_from_file((char *) &nL); // Should be 04
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Should be 00
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m1);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m2);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m3);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m4);
                        if (state == 0) break;
                        thisDefaultUnit = absHorizontalUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpih / (float) 360; // Default for command is 1/360 inch units
                        xpos2 = ((m4 * 256 * 256 * 256) + (m3 * 256 *256) + (m2 * 256) + m1) * (int) thisDefaultUnit + marginleftp;
                        if (xpos2 > marginrightp) {
                            // No action
                        } else {
                            xpos = xpos2;
                        }
                        break;
                    }
                    break;
                case '$':    // Set absolute horizontal print position ESC $ nL nH
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    if (useExtendedSettings) {
                        thisDefaultUnit = absHorizontalUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                    } else {
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpih / (float) 60; // Default for command is 1/180 inch units in LQ mode
                    }
                    xpos2 = ((nH * 256) + nL) * (int) thisDefaultUnit + marginleftp;
                    if (xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }
                    break;
                case 92:   // Set relative horizonal print position ESC \ nL nH
                    state = read_byte_from_file((char *) &nL); // always 2
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH); // always 0
                    if (state == 0) break;
                    thisDefaultUnit = defaultUnit;
                    if (defaultUnit == 0) {
                        if (letterQuality == 1) {
                            thisDefaultUnit = printerdpih / (float) 180; // Default for command is 1/180 inch units in LQ mode
                        } else {
                            thisDefaultUnit = printerdpih / (float) 120; // Default for command is 1/120 inch units in draft mode
                        }
                    }
                    advance = nH * 256 + nL;
                    if (advance >= 32768) {
                        // Handle negative movement nH > 127
                        advance = (128 * 256) - advance;
                    }
                    xpos2 = xpos + advance * (int) thisDefaultUnit;                    
                    if (xpos2 < marginleftp || xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }
                    break;
                case 'r':
                    // ESC r n Set printing colour (n=0-7)
                    // (Black, Magenta, Cyan, Violet, Yellow, Red, Green, White)
                    state = read_byte_from_file((char *) &printColour);
                    break;
                case 25:    // ESC EM n Control paper loading / ejecting (do nothing)
                    state = read_byte_from_file((char *) &nL);
                    break;
                }
            }

        } else if (print_uppercontrolcodes == 1 && (useItalicsCharSet || use8bitchars) && (xd >= (int) 128) && (xd <= (int) 159)) {
            print_character(xd);
        } else if (print_controlcodes == 1 && (xd <= (int) 127)) {
            print_character(xd);
        } else {
            // Epson ESC/P2 printer code handling
            switch (xd) {
            case 0:    // NULL do nothing
            case 128:
                break;
            case 1:    // SOH do nothing
            case 129:
                break;
            case 2:    // STX do nothing
            case 130:
                break;
            case 3:    // ETX do nothing
            case 131:
                break;
            case 4:    // EOT do nothing
            case 132:
                break;
            case 5:    // ENQ do nothing
            case 133:
                break;
            case 6:    // ACK do nothing
            case 134:
                break;
            case 7:    // BEL do nothing
            case 135:
                break;
            case 8:    // BS (BackSpace)
            case 136:
                xposold = xpos;
                hPixelWidth = printerdpih / (float) cdpih;
                if (letterQuality == 1) {
                    hPixelWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 24);
                } else {
                    hPixelWidth = (float) hPixelWidth * (((float) 120 / (float) cpi) / (float) 8);
                }
                xpos -= hPixelWidth * 8;
                if (xpos < 0) xpos = xposold;
                break;
            case 9:    // TAB
            case 137:
                curHtab = -1;
                for (i = 0; i < 32; i++) {
                    if (marginleftp + hTabulators[i] <= xpos) {
                        curHtab = i;
                    } else {
                        break;
                    }
                }
                curHtab++;
                if (curHtab > 31 || hTabulators[curHtab] == 0) {
                    // No more tab marks
                } else if (hTabulators[curHtab] > 0 && hTabulators[curHtab] <= marginrightp) {
                    // forward to next tab position
                    xpos = marginleftp + hTabulators[curHtab];
                }
                break;
            case 10:    // lf (0x0a)
            case 138:
                ypos += line_spacing;
                xpos = marginleftp;
                double_width_single_line = 0;
                test_for_new_paper();
                break;
            case 11:    // VT vertical tab (same like 10)
            case 139:
                xpos = marginleftp;
                curVtab = -1;
                for (i = (vFUChannel * 16); i < (vFUChannel * 16) + 16; i++) {
                    if (vTabulators[i] <= ypos) {
                        curVtab = i;
                    } else {
                        break;
                    }
                }
                curVtab++;
                if (curVtab > (vFUChannel * 16) + 15 || vTabulators[curVtab] == 0) {
                    // If all tabs cancelled then acts as a CR
                    // If no tabs have been set since turn on / ESC @ command, then acts like LF
                    // If tabs have been set but no more vertical tabs, then acts like FF
                    if (vTabulatorsCancelled) {
                        // CR
                        xpos = marginleftp;
                    } else if (vTabulatorsSet) {
                        // FF
                        ypos = pageSetHeight + 1;  // just put it in an out of area position
                        test_for_new_paper();
                        i = 0;
                        double_width_single_line = 0;
                    } else {
                        // LF
                        curVtab = 0; // No more tab marks
                        ypos += line_spacing;
                        double_width_single_line = 0;
                    }
                } else if (vTabulators[curVtab] > 0) {
                    // forward to next tab position
                    // ignore IF print position would be moved to inside the bottom margin
                    ypos2 = ypos;
                    ypos = vTabulators[curVtab];
                    if (ypos > marginbottomp) {
                        // Do nothing
                        ypos = ypos2;
                    } else {
                        double_width_single_line = 0;
                    }
                }
                break;
            case 12:    // form feed (neues blatt)
            case 140:
                ypos = pageSetHeight + 1;  // just put it in an out of area position
                test_for_new_paper();
                i = 0;
                double_width_single_line = 0;
                break;
            case 13:    // cr (0x0d)
            case 141:
                xpos = marginleftp;
                if (auto_LF) {
                    ypos += line_spacing;
                    double_width_single_line = 0;
                    test_for_new_paper();
                }
                break;
            case 14:    // SO Shift Out (do nothing) Select double Width printing (for one line)
            case 142:
                hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                if (multipoint_mode == 0) double_width_single_line = 1;
                break;
            case 15:    // SI Shift In (do nothing) Condensed printing on
            case 143:
                hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                if (multipoint_mode == 0) {
                    if (pitch==10) cpi=17.14;
                    if (pitch==12) cpi=20;
                    // Add for proportional font = 1/2 width - to be written
                }
                break;
            case 16:    // DLE Data Link Escape (do nothing)
            case 144:
                break;
            case 17:    // DC1 (Device Control 1)
            case 145:
                // Intended to turn on or start an ancillary device, to restore it to
                // the basic operation mode (see DC2 and DC3), or for any
                // other device control function.
                break;
            case 18:    // DC2 (Device Control 2) Condensed printing off, see 15
            case 146:
                hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                if (pitch==10) cpi=10;
                if (pitch==12) cpi=12;
                // Add for proportional font = full width
                break;
            case 19:    // DC3 (Device Control 3)
            case 147:
                // Intended for turning off or stopping an ancillary device. It may be a
                // secondary level stop such as wait, pause,
                // stand-by or halt (restored via DC1). Can also perform any other
                // device control function.
                break;
            case 20:    // DC4 (Device Control 4)
            case 148:
                // Intended to turn off, stop or interrupt an ancillary device, or for
                // any other device control function.
                // Also turns off double-width printing for one line
                hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                double_width_single_line = 0;
                break;
            case 21:    // NAK Negative Acknowledgement (do nothing)
            case 149:
                break;
            case 22:    // Syn Synchronus idle (do nothing)
            case 150:
                break;
            case 23:    // ETB End Of Transmition Block (do nothing)
            case 151:
                break;
            case 24:    // CAN Cancel (do nothing)
            case 152:
                // Not implemented - normally wipes the current line of all characters and graphics
                xpos = marginleftp;
                break;
            case 25:    // EM End Of Medium (do nothing)
            case 153:
                break;
            case 26:    // SUB Substitute (do nothing)
            case 154:
                break;
            case 27:    // ESC Escape (do nothing, will be processed later in this code)
            case 155:
                break;
            case 28:    // FS File Separator (do nothing)
                break;
            case 29:    // GS Group Separator (do nothing)
                break;
            case 30:    // RS Record Separator (do nothing)
                break;
            case 31:    // US Unit Separator (do nothing)
                break;
            case 127:    // DEL (do nothing)
                // Not implemented - normally deletes the last character to be printed on the current line
                // ignore if follows ESC $ ESC \ or HT
                break;
            case 255:
                xposold = xpos;
                hPixelWidth = printerdpih / (float) cdpih;
                vPixelWidth = printerdpiv / (float) cdpiv;
                int charHeight = 24;
                if (letterQuality == 1) {
                    hPixelWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 24);
                    vPixelWidth = (float) vPixelWidth * ((float) charHeight / (float) 16);
                } else {
                    hPixelWidth = (float) hPixelWidth * (((float) 120 / (float) cpi) / (float) 8);
                    vPixelWidth = (float) vPixelWidth * ((float) charHeight / (float) 16);
                }
                xpos += hPixelWidth * 8;
                if (xpos > ((pageSetWidth - 1) - vPixelWidth * 16)) {
                    xpos = marginleftp;
                    ypos += vPixelWidth * 16;
                }
                break;
            default:
                print_character(xd);
                break;
            }

            // ESc Branch
            if (xd == (int) 27 || xd == (int) 155 ) {   // ESC v 27 v 1b
                state = read_byte_from_file((char *) &xd);

                switch (xd) {
                case '@':    // ESC @ Initialize
                    cpi                    =  10;
                    pitch                  =  10;
                    multipoint_mode        =   0;
                    graphics_mode          =   0;
                    hmi                    = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    line_spacing           = printerdpiv * ((float) 1 / (float) 6); // normally 1/6 inch line spacing
                    chrSpacing             =   0;
                    dpih                   = 240;
                    dpiv                   = 216;
                    letterQuality          =   0;
                    proportionalSpacing    =   0;
                    printColour            =   0;
                    bold                   =   0;
                    italic                 =   0;
                    underlined             =   0;
                    superscript            =   0;
                    subscript              =   0;
                    strikethrough          =   0;
                    overscore              =   0;
                    double_width           =   0;
                    double_width_single_line = 0;
                    double_height          =   0;
                    quad_height            =   0;
                    outline_printing       =   0;
                    shadow_printing        =   0;
                    print_controlcodes     =   0;
                    print_uppercontrolcodes =  0;
                    if (use8bitchars) print_uppercontrolcodes = 1;
                    vTabulatorsSet         =   0;
                    vTabulatorsCancelled   =   0;
                    defaultUnit            =   0;
                    pageManagementUnit     =   0;
                    relHorizontalUnit      =   0;
                    absHorizontalUnit      =   0;
                    relVerticalUnit        =   0;
                    absVerticalUnit        =   0;
                    break;
                case '0':    //Select 1/8 inch line spacing
                    line_spacing = printerdpiv * ((float) 1 / (float) 8);
                    break;
                case '1':    //Select 7/72 inch line spacing
                    line_spacing = printerdpiv * ((float) 7 / (float) 72);
                    break;
                case '2':    // Select 1/6-inch line spacing
                    line_spacing = printerdpiv * ((float) 1 / (float) 6);
                    break;
                case '3':    // Set n/180-inch line spacing ESC 3 n (24 pin printer - ESC/P2 or ESC/P) or
                    // n/216 inches line spacing (9 pin printer)
                    state = read_byte_from_file((char *) &xd);
                    if (needles == 9) {
                        line_spacing = printerdpiv * ((float) xd / (float) 216);
                    } else {  // needles must be 24 here
                        line_spacing = printerdpiv * ((float) xd / (float) 180);
                    }
                    break;
                case '+':    // Set n/360-inch line spacing ESC + n
                    state = read_byte_from_file((char *) &xd);
                    line_spacing = printerdpiv * ((float) xd / (float) 360);
                    break;
                case 'A':    // ESC A n Set n/60 inches (24 pin printer - ESC/P2 or ESC/P) or
                    // n/72 inches line spacing (9 pin printer)
                    state = read_byte_from_file((char *) &xd);
                    if (needles == 9) {
                        line_spacing = printerdpiv * ((float) xd / (float) 72);
                    } else {  // needles must be 24 here
                        line_spacing = printerdpiv * ((float) xd / (float) 60);
                    }
                    break;
                case 'M':    // ESC M Select 10.5-point, 12-cpi
                    // Note - if printer in proportional mode, only takes effect when exits proportional mode
                    multipoint_mode = 0;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    cpi = 12;
                    pitch=12;
                    break;
                case 'P':     // ESC P Set 10.5-point, 10-cpi
                    // Note - if printer in proportional mode, only takes effect when exits proportional mode
                    multipoint_mode = 0;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    cpi = 10;
                    pitch=10;
                    break;
                case 'g':    // ESC g Select 10.5-point, 15-cpi
                    // Note - if printer in proportional mode, only takes effect when exits proportional mode
                    multipoint_mode = 0;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    cpi = 15;
                    pitch=15;
                    break;
                case 'l':    // ESC l m set the left margin m in characters
                    state = read_byte_from_file((char *) &xd);
                    marginleft = (int) xd;
                    marginleftp = (printerdpih / (float) cpi) * (float) marginleft;  // rand in pixels
                    // von links
                    // Wenn Marginleft ausserhalb des bereiches dann auf 0 setzen
                    // (fehlerbehandlung)
                    if (marginleftp > pageSetWidth) marginleftp = 0;
                    break;
                case 'D':    // ESC D n1,n2,n3 .... nk NUL, horizontal tab
                    // positions n1..nk
                    i = 0;
                    while ((xd != 0) && (i < 32)) {
                        // maximum 32 tabs are allowed last
                        // tab is always 0 to finish list
                        state = read_byte_from_file((char *) &xd);
                        xd = (printerdpih / (float) cpi) * (float) xd; // each tab is specified in number of characters in current character pitch
                        if (i > 0 && xd < hTabulators[i-1]) {
                            // Value less than previous tab setting ends the settings like NUL
                            xd = 0;
                        }
                        hTabulators[i] = xd;
                        i++;
                    }
                    if (i == 0 && xd == 0) {
                        // ESC D NUL cancels all horizontal tabs
                        for (i = 0; i < 32; i++) hTabulators[i] = 0;
                    }
                    break;
                case 'B':    // ESC B n1,n2,n3 .... nk NUL, vertical tab
                    // positions n1..nk - only affects vFUChannel 0
                    i = 0;
                    vTabulatorsSet = 1;
                    while ((xd != 0) && (i < 16)) {
                        // maximum 16 tabs are allowed
                        // last tab is always 0 to finish list
                        state = read_byte_from_file((char *) &xd);
                        vTabulators[i] = xd * line_spacing;
                        i++;
                    }
                    if (i == 0 && xd == 0) {
                        // ESC B NUL cancels all vertical tabs in vFUChannel 0
                        for (i = 0; i < 16; i++) vTabulators[i] = 0;
                        vTabulatorsCancelled = 1;
                    }
                    break;
                case 'b':    // ESC b m, n1,n2,n3 .... nk NUL, vertical tab input VFU channels
                    // positions n1..nk
                    i = 0;
                    vTabulatorsSet = 1;
                    // m specifies set of vertical tabs (0 to 7) - default is 0 (used by ESC B)
                    state = read_byte_from_file((char *) &m);
                    while ((xd != 0) && (i < 16)) {
                        // maximum 16 vertical tabs are allowed in each VFU Channel
                        // Last tab is always 0 to finish list
                        state = read_byte_from_file((char *) &xd);
                        vTabulators[(m * 16) + i] = xd * line_spacing;
                        i++;
                    }
                    if (i == 0 && xd == 0) {
                        // ESC b NUL cancels all vertical tabs in current vFUChannel
                        for (i = 0; i < 16; i++) vTabulators[(m * 16) + i] = 0;
                        vTabulatorsCancelled = 1;
                    }
                    break;
                case '/':    // ESC / m, set vertical tab channel
                    state = read_byte_from_file((char *) &m);
                    vFUChannel = m;
                    break;
                case 'e':    // ESC e m n, set fixed tab increment
                    state = read_byte_from_file((char *) &m);
                    state = read_byte_from_file((char *) &nL);
                    step = nL;
                    if (m == 0) {
                        // Set vertical tabs every n lines
                        if (nL * line_spacing < pageSetHeight) {
                            // maximum 16 tabs * 8 VFU Channels are allowed
                            vTabulatorsSet = 1;
                            for (m = 0; m < 8; m++) {
                                for (i = 0; i < 16; i++) {
                                    vTabulators[(m*16) + i] = nL * line_spacing;
                                    nL = nL + step;
                                }
                            }
                        }
                    } else {
                        // Set horizontal tabs every n characters in current pitch
                        if ((cpi == 10) && (nL <=21) || (cpi == 12) && (nL <=25) || (cpi >= 15) && (nL <=36)) {
                            for (i = 0; i < 32; i++) {
                                hTabulators[i] = nL;
                                nL = nL + step;
                            }
                        }
                    }
                    break;
                case 'a':    // ESC a n, set justification for text
                    // Not implemented
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 'Q':    // ESC Q m set the right margin
                    state = read_byte_from_file((char *) &xd);
                    marginright = (int) xd;
                    marginrightp = (printerdpih / (float) cpi) * (float) marginright;  // rand in pixels
                    // von links
                    break;
                case 'J':    // ESC J m Forward paper feed m/180 inches (ESC/P2)
                    state = read_byte_from_file((char *) &xd);
                    ypos += (float) xd * (printerdpiv / (float) 180);
                    test_for_new_paper();
                    break;
                case '?':    // ESC ? n m re-assign bit image mode
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &m);
                    if (state == 0) break;
                    switch (nL) {
                        case 75:
                            escKbitDensity = m;
                            break;
                        case 76:
                            escLbitDensity = m;
                            break;
                        case 89:
                            escYbitDensity = m;
                            break;
                        case 90:
                            escZbitDensity = m;
                            break;
                    }
                    break;
                case 'K':    // ESC K nL nH d1 d2...dk Select 60 dpi graphics
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escKbitDensity, dotColumns);
                    // if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
                    break;
                case 'L':    // ESC L nL nH d1 d2...dk Select 120 dpi graphics
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escLbitDensity, dotColumns);
                    break;
                case 'Y':    // ESC Y nL nH d1 d2...dk Select 120 dpi double-speed graphics
                    // Adjacent printing disabled
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escYbitDensity, dotColumns);
                    break;
                case 'Z':    // ESC Z nL nH d1 d2...dk Select 240 dpi graphics
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escZbitDensity, dotColumns);
                    break;
                case '^':
                    // ESC ^ m nL nH d1 d2 d3 d...  Select 60 oe 120 dpi, 9 pin graphics
                    state = read_byte_from_file((char *) &m);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    hPixelWidth = printerdpih / (float) 60;
                    if (m == 1) hPixelWidth = printerdpih / (float) 120;
                    vPixelWidth = printerdpiv / (float) 72;
                    line_spacing = 9 * vPixelWidth; // Fix - ensure that the line spacing is set to the same as 9 pin 72 dpi resolution in case it defaults to 60 or 120 dpi
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    _9pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
                    break;
                case 'V':    // ESC V n d1 d2 d3 d....  Repeat data n number of times
                    // Data allows up to 2047 extra bytes - last byte is identified with ESC V NUL
                    // Not currently supported - only used by LQ-1500 and SQ-2000 printers
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    // if (nL == 0) bufferData = 0;
                    break;
                case 'S':    // ESC S n select superscript/subscript printing
                    state = read_byte_from_file((char *) &nL);
                    if ((nL==1) || (nL==49)) {
                        subscript   = 1;
                        superscript = 0;
                    } else if ((nL== 0) || (nL==48)) {
                        superscript = 1;
                        subscript   = 0;
                    }
                    break;
                case 'q':    // ESC q n select character style
                    // Not yet implemented
                    state = read_byte_from_file((char *) &nL);
                    if ((nL==0)) {
                        outline_printing = 0;
                        shadow_printing = 0;
                    } else if (nL==1) {
                        outline_printing = 1;
                    } else if (nL==2) {
                        shadow_printing = 1;
                    } else if (nL==3) {
                        outline_printing = 1;
                        shadow_printing = 1;
                    }
                    break;
                case 'T':    // ESC T cancel superscript or subscript
                    subscript = 0;
                    superscript=0;
                    break;
                case 'x':    // Select LQ or draft ESC x n
                    // n can be 0 or 48 for draft
                    // and 1 or 49 for letter quality
                    // NB cancels double strike printing whilst in LQ mode (turned back on after cancel LQ Mode)
                    // Not currently implemented
                    state = read_byte_from_file((char *) &nL);
                    if ((nL==1) || (nL==49)) {
                        letterQuality = 1;
                    } else if ((nL== 0) || (nL==48)) {
                        letterQuality = 0;
                    }
                    break;
                case 'p':    // ESC p n Turn proportional mode on/off off--> n=0
                    // or 48 on --> n=1 or 49
                    // not implemented yet
                    multipoint_mode = 0;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    state = read_byte_from_file((char *) &nL);
                    if ((nL==1) || (nL==49)) {
                        letterQuality = 1;
                        proportionalSpacing = 1;
                    } else if ((nL== 0) || (nL==48)) {
                        letterQuality = 0;
                        proportionalSpacing = 0;
                        // update pitch if necessary according to ESC M, ESC g or ESC P
                    }
                    break;
                case 'E':    // ESC E SELECT BOLD FONT
                    bold = 1;
                    break;
                case 'F':    // ESC F CANCEL BOLD FONT
                    bold = 0;
                    break;
                case 'G':    // ESC G SELECT DOUBLE STRIKE
                    // For now this is same as setting bold
                    bold = 1;
                    break;
                case 'H':    // ESC H CANCEL DOUBLE STRIKE
                    // For now this is same as cancelling bold
                    bold = 0;
                    break;
                case '-':    // ESC - SELECT UNDERLINED FONT
                    state = read_byte_from_file((char *) &nL);
                    if ((nL==1) || (nL==49)) underlined=1;
                    if ((nL==0) || (nL==48)) underlined=0;
                    break;
                case 'W':    // ESC W SELECT DOUBLE WIDTH
                    state = read_byte_from_file((char *) &nL);
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (multipoint_mode == 0) {
                        if ((nL==1) || (nL==49)) double_width=1;
                        if ((nL==0) || (nL==48)) {
                            double_width=0;
                            double_width_single_line = 0;
                        }
                    }
                    break;
                case 'w':    // ESC w SELECT DOUBLE HEIGHT
                    state = read_byte_from_file((char *) &nL);
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (multipoint_mode == 0) {
                        if ((nL==1) || (nL==49)) {
                            double_height=1;
                            quad_height=0;
                        }
                        if ((nL==0) || (nL==48)) double_height=0;
                    }
                    break;
                case 'h':    // ESC h ENLARGE - STAR NL-10 implementation
                    state = read_byte_from_file((char *) &nL);
                    if (multipoint_mode == 0) {
                        switch (nL) {
                        case 0:
                            double_height=0;
                            quad_height=0;
                            break;
                        case 1:
                            double_height=1;
                            quad_height=0;
                            break;
                        case 2:
                            double_height=0;
                            quad_height=1;
                            break;
                        }
                    }
                    break;
                case '4':    // ESC 4 SELECT ITALIC FONT
                    italic = 1;
                    break;
                case '5':    // ESC 5 CANCEL ITALIC FONT
                    italic = 0;
                    break;
                case '!':    // ESC ! n Master Font Select
                    multipoint_mode = 0;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    state = read_byte_from_file((char *) &nL);
                    if ( isNthBitSet(nL, 0) ) {
                        // 12 CPI
                        cpi = 12;
                        pitch=12;
                    } else {
                        // 10 CPI
                        cpi = 10;
                        pitch=10;
                    }
                    if ( isNthBitSet(nL, 1) ) {
                        // Select proportional - see ESC p 1 - not implemented yet
                    } else {
                        // Cancel proportional - see ESC p 0 - not implemented yet
                    }
                    if ( isNthBitSet(nL, 2) ) {
                        // Select Condensed mode
                        if (pitch==10) cpi=17.14;
                        if (pitch==12) cpi=20;
                    } else {
                        // Cancel Condensed mode
                        if (pitch==10) cpi=10;
                        if (pitch==12) cpi=12;
                    }
                    if ( isNthBitSet(nL, 3) ) {
                        // Select Bold
                        bold = 1;
                    } else {
                        // Cancel Bold
                        bold = 0;
                    }
                    if ( isNthBitSet(nL, 4) ) {
                        // Select Double Strike - for now, same as bold
                        bold = 1;
                    } else {
                        // Cancel Double Strike
                        bold = 0;
                    }
                    if ( isNthBitSet(nL, 5) ) {
                        // Select Double Width
                        double_width = 1;
                    } else {
                        // Cancel Double Width
                        double_width = 0;
                        double_width_single_line = 0;
                    }
                    if ( isNthBitSet(nL, 6) ) {
                        // Select Italics
                        italic = 1;
                    } else {
                        // Cancel Italics
                        italic = 0;
                    }
                    if ( isNthBitSet(nL, 7) ) {
                        // Select Underline
                        underlined=1;
                    } else {
                        // Cancel Underline
                        underlined=0;
                    }
                    break;
                case '(':
                    state = read_byte_from_file((char *) &nL);
                    switch (nL) {
                    case '-':
                        // ESC ( - nl nh m d1 d2  Select line/score
                        state = read_byte_from_file((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *)  &m);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &d1);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &d2);
                        if (state == 0) break;
                        if (m==1) {
                            switch (d1) {
                            // Re-written 30/4/17 - as you can combine it so for example
                            // underlined - single broken_line, with overscore double_continuous line
                            case 1:
                                // Underlined
                                switch (d2) {
                                case 0:
                                    //Turn off
                                    underlined = 0;
                                    break;
                                case 1:
                                    underlined = 1; // single_continuous_line
                                    break;
                                case 2:
                                    underlined = 2; // double_continuous_line
                                    break;
                                case 5:
                                    underlined = 3; // single_broken_line
                                    break;
                                case 6:
                                    underlined = 4; // double_broken_line
                                    break;
                                }
                                break;
                            case 2:
                                // Strikethrough
                                switch (d2) {
                                case 0:
                                    //Turn off
                                    strikethrough = 0;
                                    break;
                                case 1:
                                    strikethrough = 1; // single_continuous_line
                                    break;
                                case 2:
                                    strikethrough = 2; // double_continuous_line
                                    break;
                                case 5:
                                    strikethrough = 3; // single_broken_line
                                    break;
                                case 6:
                                    strikethrough = 4; // double_broken_line
                                    break;
                                }
                                break;
                            case 3:
                                // Overscore
                                switch (d2) {
                                case 0:
                                    //Turn off
                                    overscore = 0;
                                    break;
                                case 1:
                                    overscore = 1; // single_continuous_line
                                    break;
                                case 2:
                                    overscore = 2; // double_continuous_line
                                    break;
                                case 5:
                                    overscore = 3; // single_broken_line
                                    break;
                                case 6:
                                    overscore = 4; // double_broken_line
                                    break;
                                }
                                break;
                            }

                            if (!quiet_mode) {
                                printf("underlined  %d\n",underlined);
                                printf("strikethrough  %d\n",strikethrough);
                                printf("overscore  %d\n",overscore);
                            }
                        }
                        break;
                    case 'C':
                        // 27 40 67 nL nH mL mH Set page length in defined unit
                        // page size not implemented yet
                        state = read_byte_from_file((char *) &nL); // always 2
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // always 0
                        if (state == 0) break;
                        if (nL != 4) {
                            // Original ESC/P2 standard
                            state = read_byte_from_file((char *) &mL);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mH);
                            if (useExtendedSettings) {
                                thisDefaultUnit = pageManagementUnit;
                            } else {
                                thisDefaultUnit = defaultUnit;
                            }
                            if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                            /* TO DO
                            int pageLength = ((mH * 256) + mL) * (int) thisDefaultUnit;
                            if (pageLength > pageSetHeight) {
                                // Free more memory and reset the pagesetheight and default margins
                            }
                            */
                        } else if (nL == 4) {
                            // Extended standard
                            state = read_byte_from_file((char *) &m1);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m2);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m3);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m4);
                            if (state == 0) break;
                            thisDefaultUnit = pageManagementUnit;
                            if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                            /* TO DO
                            int pageLength = ((m4 * 256 * 256 * 256) + (m3 * 256 *256) + (m2 * 256) + m1) * (int) thisDefaultUnit;
                            if (pageLength > pageSetHeight) {
                                // Free more memory and reset the pagesetheight and default margins
                            }
                            */
                        }
                        margintopp = defaultMarginTopp;
                        marginbottomp = defaultMarginBottomp;
                        ypos = margintopp;
                        break;
                    case 'c':
                        // 27 40 67 nL nH tL tH bL bH Set page format
                        state = read_byte_from_file((char *) &nL); // always 4
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // always 0
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &tL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &tH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &bL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &bH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        margintopp = ((tH * 256) + tL) * (int) thisDefaultUnit;
                        marginbottomp = ((bH * 256) + bL) * (int) thisDefaultUnit;
                        if (marginbottomp > 22 * printerdpih) marginbottomp = 22 * printerdpih; // Max 22 inches
                        if (marginbottomp > pageSetHeight) marginbottomp = pageSetHeight;
                        ypos = margintopp;
                        // cancel top and bottom margins (margintopp and marginbottomp - to be implemented)
                        break;
                    case 'V':
                        // 27 40 86 nL nH mL mH Set absolute vertical print position
                        state = read_byte_from_file((char *) &nL); // Always 2
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Always 0
                        if (state == 0) break;
                        if (nL != 4) {
                            // Original ESC/P2 standard
                            state = read_byte_from_file((char *) &mL);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mH);
                            if (useExtendedSettings) {
                                thisDefaultUnit = absVerticalUnit;
                                if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                                ypos2 = margintopp + ((mH * 256) + mL) * (int) thisDefaultUnit;
                            } else {
                                thisDefaultUnit = defaultUnit;
                                if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                                ypos2 = margintopp + ((mH * 256) + mL) * (int) thisDefaultUnit;
                            }
                            // Ignore if movement is more than 179/360" upwards
                            if (ypos2 < (ypos - (printerdpiv * ((float) 179/(float) 360))) ) ypos2 = ypos;
                            // Ignore if command would move upwards after graphics command sent on current line, or above where graphics have
                            // previously been printed - to be implemented
                        } else if (nL == 4) {
                            // Extended standard
                            state = read_byte_from_file((char *) &m1);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m2);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m3);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m4);
                            if (state == 0) break;
                            thisDefaultUnit = absVerticalUnit;
                            if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                            ypos2 = margintopp + ((m4 * 256 * 256 * 256) + (m3 * 256 *256) + (m2 * 256) + m1) * (int) thisDefaultUnit;
                            // Ignore if movement is negative
                            if (ypos2 < ypos) ypos2 = ypos;
                        }
                        ypos = ypos2;
                        test_for_new_paper();
                        break;
                    case 'v':
                        // 27 40 118 nL nH mL mH Set relative vertical print position
                        state = read_byte_from_file((char *) &nL); // Always 2
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Always 0
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &mL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &mH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        advance = mH * 256 + mL;
                        if (advance >= 32768) {
                            // Handle negative movement mH > 127
                            advance = (128 * 256) - advance;
                        }
                        ypos2 = ypos + advance * (int) thisDefaultUnit; 
                        // Ignore if movement is more than 179/360" upwards
                        if (ypos2 < (ypos - (printerdpiv * ((float) 179/(float) 360))) ) ypos2 = ypos;
                        // ignore if command would move upwards after graphics command sent on current line, or above where graphics have
                        // previously been printed - to be written
                        if (ypos2 < margintopp) {
                            // No action
                        } else {
                            ypos = ypos2;
                            test_for_new_paper();
                        }
                        break;
                    case 'U':
                        // 27 40 85 nL nH m Set unit
                        state = read_byte_from_file((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Always 0
                        if (state == 0) break;
                        if (nL != 5) {
                            // Original ESC/P2 standard
                            int useExtendedSettings = 0;
                            state = read_byte_from_file((char *) &m);
                            defaultUnit = ((float) m / (float) 3600) * printerdpiv; // set default unit to m/3600 inches
                            // extended standard
                            pageManagementUnit = defaultUnit;
                            relHorizontalUnit = defaultUnit;
                            absHorizontalUnit = defaultUnit;
                            relVerticalUnit = defaultUnit;
                            absVerticalUnit = defaultUnit;
                        } else if (nL == 5) {
                            // Extended standard
                            int useExtendedSettings = 1;
                            state = read_byte_from_file((char *) &m1); // P
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m2); //  V
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &m3); // H
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mL);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &mH);
                            if (state == 0) break;
                            pageManagementUnit = ((float) m1 / (float) ((mH * 256) + mL) * printerdpiv);
                            relVerticalUnit = ((float) m2 / (float) ((mH * 256) + mL) * printerdpiv);
                            absVerticalUnit = ((float) m2 / (float) ((mH * 256) + mL) * printerdpiv);
                            relHorizontalUnit = ((float) m3 / (float) ((mH * 256) + mL) * printerdpiv);
                            absHorizontalUnit = ((float) m3 / (float) ((mH * 256) + mL) * printerdpiv);
                        }
                        break;
                    case 'i':
                        // ESC ( i 01 00 n  Select microweave print mode
                        state = read_byte_from_file((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m);
                        if ((nL == 1) && (nH == 0) ) {
                            if ((m == 0) || (m == 48)) microweave_printing = 0;
                            if ((m == 1) || (m == 49)) microweave_printing = 1;
                        }
                        break;
                    case '$':
                        // ESC ( $ 04 00 m1 m2 m3 m4 Set absolute horizontal print position
                        // Only effective in graphics mode
                        state = read_byte_from_file((char *) &nL); // Should be 04
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH); // Should be 00
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m1);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m2);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m3);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m4);
                        if (state == 0) break;
                        // DO NOTHING - not in graphics mode
                        break;
                    case 't':
                        // 27 40 116 nL nH d1 d2 d3 Assign character table
                        // not implemented yet
                        state = read_byte_from_file((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &d1);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &d2);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &d3);
                        break;
                    case '^':
                        // ESC ( ^ nL nH d1 d2 d3 d...  print data as characters (ignore control codes)
                        state = read_byte_from_file((char *) &nL); // NULL
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH);
                        if (state == 0) break;
                        m = (nH * 256) + nL;
                        j = 0;
                        while (j < m) {
                            // read data and print as normal
                            // ignore if no character assigned to that character code in current character table
                            state = read_byte_from_file((char *) &xd);
                            print_character(xd);
                            j++;
                        }
                        break;
                    case 'G':
                        // ESC ( G nL nH m  Select graphics mode
                        state = read_byte_from_file((char *) &nL); // NULL
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_file((char *) &m);
                        if ((nL == 1) && (nH == 0) && ((m == 1) || (m == 49)) ) {
                            graphics_mode = 1;
                            // clear all user defined graphics - To be implemented
                            // Clear tab marks
                            for (i = 0; i < 32; i++) hTabulators[i] = 0;
                            for (i = 0; i < (8 * 16); i++) vTabulators[i] = 0;
                            microweave_printing = 0;
                            vTabulatorsCancelled = 0;
                            vTabulatorsSet = 0;
                        }
                        break;
                    }
                    break;
                case 't':
                    // ESC t n Select character table
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 'R':
                    // ESC R n Select international character set
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    break;
                case '&':
                    // ESC & NULL nL m a0 a1 a2 d1 d2 d3 d...  Define user defined characters
                    // not implemented yet - Needs more work because number of d1.... codes varies and not clear how to calculate!!
                    state = read_byte_from_file((char *) &nL); // NULL
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &m);
                    if (state == 0) break;
                    if (m < nL) break;
                    i = 0;
                    if (needles == 9) {
                        // ESC & NULL nL m a0 d1 d2 d3 d... for draft
                        // ESC & NULL nL m 0 a0 0 d1 d2 d3 d... for NLQ
                        state = read_byte_from_file((char *) &a0);
                        if (state == 0) break;
                        if (a0 == 0) {
                            // NLQ
                            while (i < (m-nL)+1) {
                                state = read_byte_from_file((char *) &a0);
                                if (state == 0) break;
                                state = read_byte_from_file((char *) &xd);
                                if (state == 0) break;
                                // Should be 0 returned
                                j = 0;
                                // Check - should this be a0
                                while (j < a1 * 3) {
                                    state = read_byte_from_file((char *) &xd);
                                    if (state == 0) break;
                                    j++;
                                }
                                i++;
                            }
                        } else {
                            // DRAFT
                            // Get first remaining bit of data, then repeat
                            j = 0;
                            // Check - should this be a0
                            while (j < a1 * 3) {
                                state = read_byte_from_file((char *) &xd);
                                if (state == 0) break;
                                j++;
                            }
                            i++;
                            while (i < (m-nL)+1) {
                                state = read_byte_from_file((char *) &a0);
                                if (state == 0) break;
                                j = 0;
                                // Check - should this be a0
                                while (j < a1 * 3) {
                                    state = read_byte_from_file((char *) &xd);
                                    if (state == 0) break;
                                    j++;
                                }
                                i++;
                            }
                        }
                    } else {  // needles must be 24 here
                        // ESC & NULL nL m a0 a1 a2 d1 d2 d3 d...
                        while (i < (m-nL)+1) {
                            state = read_byte_from_file((char *) &a0);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &a1);
                            if (state == 0) break;
                            state = read_byte_from_file((char *) &a2);
                            if (state == 0) break;
                            j = 0;
                            // Check - we are not using a0 or a2
                            while (j < a1 * 3) {
                                state = read_byte_from_file((char *) &xd);
                                if (state == 0) break;
                                j++;
                            }
                            i++;
                        }
                    }
                    break;
                case ':':
                    // ESC : nul n m Copy ROM to RAM (for amending character codes)
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    state = read_byte_from_file((char *) &nL);
                    state = read_byte_from_file((char *) &nL);
                    break;
                case '%':
                    // ESC % n Select user-defined character set
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 'k':
                    // ESC k n Select typeface for LQ printing - ignore id user-defined character set selected
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 'f':
                    // ESC f m n Horizontal / Vertical Skip
                    state = read_byte_from_file((char *) &m);
                    state = read_byte_from_file((char *) &nL);
                    if (m == 0) {
                        // print nL spaces in current pitch - use underline if set
                        int k;
                        for (k = 0; k < nL; k++) print_space(1);
                    } else {
                        // perform nL line feeds in current line spacing cancel double width printing set with SO or ESC SO
                        ypos += nL * line_spacing;
                        xpos = marginleftp;
                        double_width_single_line = 0;
                        test_for_new_paper();
                    }
                    break;
                case 'j':
                    // ESC j n Reverse paper feed n/216 inches
                    state = read_byte_from_file((char *) &nL);
                    ypos -= (float) printerdpiv * ((float) nL /(float) 216);
                    if (ypos < margintopp) ypos = margintopp;
                    break;
                case 'c':
                    // ESC c nL nH Set Horizonal Motion Index (HMI)
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    state = read_byte_from_file((char *) &nH);
                    hmi = printerdpih * ((float) (nH *256) + (float) nL / (float) 360);
                    break;
                case 'X':
                    // ESC X m nL nH Select font by pitch & point
                    // not implemented yet
                    multipoint_mode = 1;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    state = read_byte_from_file((char *) &m);
                    state = read_byte_from_file((char *) &nL);
                    state = read_byte_from_file((char *) &nH);
                    if (m == 0) {
                        // No change in pitch
                    } else if (m==1) {
                        // Proportional spacing - not yet implemented
                    } else if (m >5) {
                        cpi = (float) 360 / (float) m;
                    }
                    if ((nH > 0) || (nL > 0)) {
                        // Set point size - 1 pt = 1/72 inch - not yet implemented
                        // pointSize = ((nH * 256) + nL) / 2
                    }
                    break;
                case 'U':    // Turn unidirectional mode on/off ESC U n n = 0 or
                    // 48 Bidirectional 1 or 49 unidirectional
                    // not required
                    state = read_byte_from_file((char *) &nL);
                    break;
                case '<':    // Unidirectional mode (1 line)
                    // not required
                    xpos = marginleftp;
                    break;
                case '#':    // Cancel MSB Control
                    msbsetting = 0;
                    break;
                case '=':    // Set MSB (bit 7) of all incoming data to 0
                    msbsetting = 1;
                    break;
                case '>':    // Set MSB (bit 7) of all incoming data to 1
                    msbsetting = 2;
                    break;
                case '8':    // Disable paper-out detector
                    // not required
                    break;
                case '9':    // Enable paper-out detector
                    // not required
                    break;
                case 'i':    // Select immediate mode on/off ESC i n
                    // not required
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 's':    // Select low-speed mode on/off ESC s n
                    // not required
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 'C':    // 27 67 n Set page length in lines 1<n<127
                    // not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    if (nL == 0) {  // 27 67 0 n Set page length in inches 1<n<22
                        state = read_byte_from_file((char *) &nL);
                    }
                    break;
                case 'N':    // Set bottom margin ESC N n 0<n<=127 top-of-form
                    // position on the next page
                    // not implemented (continuous paper only) - ignored when printing on single sheets// not implemented yet
                    state = read_byte_from_file((char *) &nL);
                    break;
                case 'O':    // Cancel bottom margin Cancels the top and bottom
                    // margin settings
                    margintopp = defaultMarginTopp;
                    marginbottomp = defaultMarginBottomp;
                    break;
                case '$':    // Set absolute horizontal print position ESC $ nL nH
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    if (useExtendedSettings) {
                        thisDefaultUnit = absHorizontalUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                    } else {
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpih / (float) 60; // Default for command is 1/180 inch units in LQ mode
                    }
                    xpos2 = ((nH * 256) + nL) * (int) thisDefaultUnit + marginleftp;
                    if (xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }
                    break;
                case 92:   // Set relative horizonal print position ESC \ nL nH
                    state = read_byte_from_file((char *) &nL); // always 2
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH); // always 0
                    if (state == 0) break;
                    thisDefaultUnit = defaultUnit;
                    if (defaultUnit == 0) {
                        if (letterQuality == 1) {
                            thisDefaultUnit = printerdpih / (float) 180; // Default for command is 1/180 inch units in LQ mode
                        } else {
                            thisDefaultUnit = printerdpih / (float) 120; // Default for command is 1/120 inch units in draft mode
                        }
                    }
                    advance = nH * 256 + nL;
                    if (advance >= 32768) {
                        // Handle negative movement nH > 127
                        advance = (128 * 256) - advance;
                    }
                    xpos2 = xpos + advance * (int) thisDefaultUnit;
                    if (xpos2 < marginleftp || xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }
                    break;
                case '6':    // ESC 6 Enable printing of upper control codes (characters 128 to 159)
                    if (italic == 0 ) {
                        print_uppercontrolcodes = 1;
                    }
                    break;
                case '7':    // ESC 7 Disable printing of upper control codes
                    print_uppercontrolcodes = 0;
                    break;
                case 'I':    // ESC I n - enable printing of control codes - shaded codes in table in manual (A-23)
                    state = read_byte_from_file((char *) &nL);
                    if (italic == 0 ) {
                        if (nL==1) print_controlcodes = 1;
                        if (nL==0) print_controlcodes = 0;
                    }
                    break;
                case 'm':    // ESC m n - Select printing of upper control codes
                    state = read_byte_from_file((char *) &nL);
                    if (italic == 0 ) {
                        if (nL==0) print_uppercontrolcodes=1;
                        if (nL==4 && !use8bitchars) print_uppercontrolcodes=0;
                    }
                    break;
                case 'r':
                    // ESC r n Set printing colour (n=0-7)
                    // (Black, Magenta, Cyan, Violet, Yellow, Red, Green, White)
                    state = read_byte_from_file((char *) &printColour);
                    break;
                case 10: // ESC LF
                    // Reverse line feed - Star NL-10
                    ypos -= line_spacing;
                    xpos = marginleftp;
                    double_width_single_line = 0;
                    test_for_new_paper();
                    break;
                case 12: // ESC FF
                    // Reverse form feed - Star NL-10
                    ypos = -1;  // just put it in an out of area position
                    xpos = marginleftp;
                    test_for_new_paper();
                    break;
                case 20:    // ESC SP Set intercharacter space
                    state = read_byte_from_file((char *) &nL);
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (multipoint_mode == 0) {
                        chrSpacing = nL;
                    }
                    break;
                case 25:    // ESC EM n Control paper loading / ejecting (do nothing)
                    state = read_byte_from_file((char *) &nL);
                    break;
                case '*':    // ESC * m nL nH d1 d2 . . . dk print bit-image graphics.
                    m = 0;
                    nL = 0;
                    nH = 0;
                    state = read_byte_from_file((char *) &m);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_file((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(m, dotColumns);
                    break;
                case 14:    // ESC SO Shift Out Select double Width printing (for one line)
                    if (multipoint_mode == 0) double_width_single_line = 1;
                    break;
                case 15:    // ESC SI Shift In Condensed printing on
                    if (multipoint_mode == 0) {
                        if (pitch==10) cpi=17.14;
                        if (pitch==12) cpi=20;
                        // Add for proportional font = 1/2 width - to be written
                    }
                    break;
                } // end of switch
            }   // End of ESC branch
        }
        if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
    }

    if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
    if (state && i == 0) goto main_loop_for_printing;

    if (!quiet_mode) printf("\n\nI am at page %d\n", page);

    sprintf(filenameX, "%spage%d.png", pathpng, page);
    int dataToConvert = write_png(filenameX, pageSetWidth, pageSetHeight, printermemory);

    if (outputFormatText == 0) {
        // No end of line conversion required
        sprintf(filenameX, "cp  %s  %s ", input_filename,patheps);
        if (quiet_mode) {
            strcat(filenameX, " >/dev/null");
        } else {
            printf("command = %s \n", filenameX);
        }
        system(filenameX);
    } else {
        sprintf(filenameX, "%s", input_filename);
        sprintf(filenameY, "%s%d.eps", patheps,page);
        convertUnixWinMac(filenameX,filenameY);
    }

    if (dataToConvert > 0) {
        sprintf(filenameX, "%spage%d.png", pathpng, page);
        sprintf(filenameY, "%spage%d.pdf", pathpdf, page);
        write_pdf(filenameX, filenameY, pageSetWidth, pageSetHeight);
    }

    if ((fp != NULL)) {
        fclose(fp);
        fp = NULL;
    }        // close text file

    // No more data to be read from file
    if (feof(inputFile)) {
        fclose(inputFile);
        free(printermemory);
        if (imagememory == seedrow) {
            free(imagememory);
        } else {
            free(imagememory);
            free(seedrow);
        }
        exit(0);
    }

  raus:
    return 0;
}
