#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <png.h>
#include "/usr/include/SDL/SDL.h"
#include "dir.c"

/* Conversion program to convert Epson ESC/P printer data to an Adobe PDF file on Linux.
 * v1.0
 *
 * v1.0 First Release - taken from the conversion software currently in development for the Retro-Printer module.
 * www.retroprinter.com
 */

// Configuration options - these are currently intended to be simple files with flags 
#define EMULATION_CONFIG    "/root/config/emulation"
#define LINEFEED_CONFIG     "/root/config/linefeed_ending"
#define PATH_CONFIG         "/root/config/output_path"

// Set page size for bitmap to A4 (8.27 x 11.69 inches).
// DIN A4 has a width 210 mm and height of 297 mm .
// On original 9 pin (ESC/P) printers, the needles have a distance of 72dpi 
// 240dpi horizontal resolution and 216dpi vertical resolution
// Was 240 x 8.27 = 1924    x   216 x 11.69 = 2525
// However 24 pin (ESC/P2) printers support up to 720dpi x 720dpi
// giving 720 x 8.27 = 5954   x   720 x 11.69 = 8417
float printerdpih = 720;
float printerdpiv = 720;
int pageSetWidth = 5954;
int pageSetHeight = 8417;
unsigned char *printermemory;

unsigned int page = 0;
char filenameX[1000];
char filenameY[1000];
char *param;                // parameters passed to program
int xdim, ydim;
int sdlon = 1;              // sdlon=0 Do not copy output to SDL screen
int state = 1;              // State of the centronics interface
int timeout = 4;            // printout finished after this time. so start to print all received data.
int countcharz;

char    path[1000];         // main path 
char    pathraw[1000];      //path to raw files
char    pathpng[1000];      //path to png files
char    pathpdf[1000];      //path to pdf files
char    pathpcl[1000];      //path to pcl files if rawispcl = 1
char    patheps[1000];      //path to eps files if rawiseps = 1
char    pathcreator[1000];  //path to usb

struct timespec tvx;
int startzeit;              // start time for time measures to avoid infinite loops

int t1=0,t2=0,t3=0,t4=0,t5=0;
int ackposition            = 0;
int msbsetting             = 0;
int rawispcl               = 0;         //if 1 the raw folder is copied to pcl folder and raw files are renamed to *.pcl in the pcl folder
int rawiseps               = 0;         //if 1 the raw folder is copied to eps folder and raw files are renamed to *.eps in the eps folder
int outputFormatText       = 0;         //0=no conversion  1=Unix (LF) 2= Windows (CR+LF) 3=MAC (CR)
int printColour            = 0;         //Default colour is black
double defaultUnit         = 0;         //Use Default defined unit
double thisDefaultUnit     = 0;         //Default unit for use by command    

int bold                   = 0;         //Currently bold and double-strike are the same
int italic                 = 0;
int underlined             = 0;
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

FILE *inputFile;
int initialize()
{
    printermemory = malloc (5955 * 8417); // Allow 3 bytes per pixel
    if (printermemory == NULL) {
        fprintf(stderr, "Can't allocate memory for PNG file.\n");
        exit (0);
    }    
    
    /* routine could be used here to open the input file or port for reading 
    *  example is for reading from an input file called ./Test1.prn
    *  The routine is not error trapped at present
    */
    inputFile = fopen("./Test1.prn", "r");
    if (inputFile == NULL) return -1;
    timeout = 0; // When reading from a file, the timeout can be set to zero
}

int read_byte_from_printer(unsigned char *bytex)
{
    /* This routine needs to be written according to your requirements
    * the routine needs to fetch the next byte from the captured printer data file (or port).
    */
    unsigned char databyte;
    // read databyte from file or port...
    // Example below is reading a character from the file opened in initialize();
    // take account of msbsetting : 0 - use bit 7 as set in data, 1 - clear bit 7 of all data, 2 - set bit 7 of all data

    if (timeout > 0) {
        clock_gettime(CLOCK_REALTIME, &tvx);
        startzeit = tvx.tv_sec;
    }
    while (!feof(inputFile)) {
        databyte = fgetc(inputFile);
        if (timeout > 0) {
            clock_gettime(CLOCK_REALTIME, &tvx);
            if (((tvx.tv_sec - startzeit) >= timeout)) return 0;
        }        
        switch (msbsetting) {
        case 0:
            // No change
            break;
        case 1:
            // MSB is set byte 7 to 0
            databyte = databyte & 127;
            break;
        case 2:
            // MSB is set byte 7 to 1
            databyte = databyte | 128;
            break;
        }
        *bytex = databyte;
        return 1;
    }

    
    return 0;
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

int isNthBitSet (unsigned char c, int n) {
  return (c >> n);
}

int * lookupColour(unsigned char colourValue)
{
    // Convert printer colour (0 to 7 stored in bits of colourValue) to RGB value
    // Not called if colourValue is White
    // Routine uses averaging to get colours such as pink (red + white)
    static int rgb1[3];
    int blackSet = 0;
    rgb1[0]=0;
    rgb1[1]=0;
    rgb1[2]=0;        
    if (colourValue == 0 || colourValue == 1) return rgb1; // Black

    if (isNthBitSet(colourValue, 0) ) {
        // Black - default
        blackSet = 1;
    }
    if (isNthBitSet(colourValue, 1) ) {
        // Magenta FF00FF
        if (blackSet || rgb1[0]>0) {
            rgb1[0] = (rgb1[0] + 255) /2;
        } else {
            rgb1[0] = 255;
        }
        if (rgb1[1]>0) rgb1[1] = (rgb1[1] + 0) /2;
        if (blackSet || rgb1[2]>0) {
            rgb1[2] = (rgb1[2] + 255) /2;
        } else {
            rgb1[2] = 255;
        }
    }
    if (isNthBitSet(colourValue, 2) ) {
        // Cyan 00FFFF
        if (rgb1[0]>0) rgb1[0] = (rgb1[0] + 0) /2;
        if (blackSet || rgb1[1]>0) {
            rgb1[1] = (rgb1[1] + 255) /2;
        } else {
            rgb1[1] = 255;
        }
        if (blackSet || rgb1[2]>0) {
            rgb1[2] = (rgb1[2] + 255) /2;
        } else {
            rgb1[2] = 255;
        }
    }     
    if (isNthBitSet(colourValue, 3) ) {
        // Violet EE82EE
        if (blackSet || rgb1[0]>0) {
            rgb1[0] = (rgb1[0] + 238) /2;
        } else {
            rgb1[0] = 238;
        }
        if (blackSet || rgb1[1]>0) {
            rgb1[1] = (rgb1[1] + 130) /2;
        } else {
            rgb1[1] = 130;
        }        
        if (blackSet || rgb1[2]>0) {
            rgb1[2] = (rgb1[2] + 238) /2;
        } else {
            rgb1[2] = 238;
        }
    }
    if (isNthBitSet(colourValue, 4) ) {
        // Yellow FFFF00
        if (blackSet || rgb1[0]>0) {
            rgb1[0] = (rgb1[0] + 255) /2;
        } else {
            rgb1[0] = 255;
        }
        if (blackSet || rgb1[1]>0) {
            rgb1[1] = (rgb1[1] + 255) /2;
        } else {
            rgb1[1] = 255;
        }
        if (rgb1[2]>0) rgb1[2] = (rgb1[2] + 0) /2;
    }
    if (isNthBitSet(colourValue, 5) ) {
        // Red FF0000
        if (blackSet || rgb1[0]>0) {
            rgb1[0] = (rgb1[0] + 255) /2;
        } else {
            rgb1[0] = 255;
        }
        if (rgb1[1]>0) rgb1[1] = (rgb1[1] + 0) /2;
        if (rgb1[2]>0) rgb1[2] = (rgb1[2] + 0) /2;
    }
    if (isNthBitSet(colourValue, 6) ) {
        // Green 00FF00
        if (rgb1[0]>0) rgb1[0] = (rgb1[0] + 0) /2;
        if (blackSet || rgb1[1]>0) {
            rgb1[1] = (rgb1[1] + 255) /2;
        } else {
            rgb1[1] = 255;
        }
        if (rgb1[2]>0) rgb1[2] = (rgb1[2] + 0) /2;
    }
    return rgb1;
}

int write_png(const char *filename, int width, int height, char *rgb)
{
    int ipos;
    int *pixelColour;
    int code = 1;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep row = NULL;
    png_byte *row_pointers[10];
    FILE *file; 
    
    char *title = "ESC/P2 converted image";

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
        fprintf(stderr, "PNG error - Could not allocate write structure\n");
        code = 0;
        goto finalise;
    }

    // Initialize info structure
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        fprintf(stderr, "PNG error - Could not allocate info structure\n");
        code = 0;
        goto finalise;
    }
    
    png_set_compression_level(png_ptr, 6);  // Minimal compression for speed - balance against time taken by convert routine

    // Setup Exception handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error during png creation\n");
        code = 0;
        goto finalise;
    }

    png_init_io(png_ptr, file);

    // Write header (8 bit colour depth)
    png_set_IHDR(png_ptr, info_ptr, width, height,
                8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    // Set title
    if (title != NULL) {
        png_text title_text;
        title_text.compression = PNG_TEXT_COMPRESSION_NONE;
        title_text.key = "Title";
        title_text.text = title;
        png_set_text(png_ptr, info_ptr, &title_text, 1);
    }

    png_write_info(png_ptr, info_ptr);
    
    // Allocate memory for a rows (3 bytes per pixel - RGB)
    row = (png_bytep) malloc(3 * width * sizeof(png_byte));
    if (row == NULL) {
        fprintf(stderr, "Can't allocate memory for PNG file.\n");
        code = 0;
        goto finalise;
    }    

    // Write image data
    int x, y, ppos, rowCount = 0;
    for (y=0 ; y<height ; y++) {
        ipos = width * y;
        for (x=0 ; x<width ; x++) {
            ppos = x * 3;
            if (rgb[ipos + x] ==128) {
                row[ppos] = 255;
                row[ppos + 1] = 255;
                row[ppos + 2] = 255;
            } else {
                pixelColour = lookupColour(rgb[ipos + x]);
                row[ppos + 0] = pixelColour[0];
                row[ppos + 1] = pixelColour[1];
                row[ppos + 2] = pixelColour[2];
            }
        }
        row_pointers[rowCount] = row;
        if (rowCount == 10) {
            png_write_rows(png_ptr, row_pointers, rowCount);
            rowCount = 0;
        } else {
            rowCount++;
        }
    }
    if (rowCount) {
        // Write last buffer
        png_write_rows(png_ptr, row_pointers, rowCount);
    }

    // End write
    png_write_end(png_ptr, NULL);    
    
finalise:
    if (file != NULL) fclose(file);
    if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if (row != NULL) free(row);

    return code;
}

void putpx(int x, int y)
{
    // Write printer colour to specific pixel on the created bitmap
    int pos = y * pageSetWidth + x;
    unsigned char existingPixel = printermemory[pos];

    // If existing pixel is white, then we need to reset it to 0 before OR'ing the chosen colour
    if (existingPixel == 128) {
        printermemory[pos] = 1 << printColour;
    } else {
        printermemory[pos] |= 1 << printColour;
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

void putpixelbig(int xpos, int ypos, float hwidth, float vdith)
{
    int a, b;
    for (a = 0; a < hwidth; a++) {
        for (b = 0; b < vdith; b++) {
            putpixel(display, xpos + a, ypos + b, 0x00000000);
        }
    }
}

int cpi = 10; // PICA is standard
int pitch = 10; //Same like cpi but will retain its value when condensed printing is switched on
int line_spacing = (float) 720 * ((float) 1 / (float) 6);     // normally 1/6 inch line spacing - (float) 30*((float)pitch/(float)cpi);
int marginleft = 0, marginright = 99;       // in characters
int marginleftp = 0, marginrightp = 5954;   // in pixels
int margintopp = 0, marginbottomp = 8417;
int cdpih = 120; // fixed dots per inch used for printing characters
int cdpiv = 144; // fixed dots per inch used for printing characters
int cpih = 10; // Default is PICA
int cpiv = 6; // Default font height in cpi (default line spacing is 1/6" inch too)
int dpih = 180, dpiv = 180;                 // resolution in dpi for ESC/P2 printers
int needles = 24;                           // number of needles
int letterQuality = 0;                      // LQ Mode?
int proportionalSpacing = 0;                // Proportional Mode (not implemented)
int rows = 0;
double xpos = 0, ypos = 0;                  // position of print head
double xpos2 = 0, ypos2 = 0;                // position of print head

float hmi = (float) 720 * ((float) 36 / (float) 360);              // pitch in inches per character.  Default is 36/360 * 720 = 10 cpi
float chrSpacing = 0;                      // Inter-character spacing

// ******bit images
int m;                                      // Mode for bit images printing (m)
int c, v, h;                                // Used for raster graphics printing
unsigned char nL, nH;                       // width of bit image line, lowbyte and high byte
unsigned char mL, mH;                       // extra parameters mode, lowbyte and high byte
unsigned char tL, tH;                       // extra parameters top margin, lowbyte and high byte
unsigned char bL, bH;                       // extra parameters bottom margin, lowbyte and high byte
unsigned char d1, d2, d3;
unsigned char a0, a1, a2;
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
char fontx[2049000];

void erasepage()
{
    int i;  
    for (i = 0; i < pageSetWidth * pageSetHeight; i++) printermemory[i] = 1 << 7;
}

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
    if ((ypos > (pageSetHeight - 17 * vPixelWidth)) || (state == 0)) {
        xpos = marginleftp;
        ypos = 0;
        sprintf(filenameX, "%spage%d.png", pathpng, page);
        printf("write   = %s \n", filenameX);
        write_png(filenameX, pageSetWidth, pageSetHeight, printermemory);
        
        // Create pdf file
        sprintf(filenameX, "convert  %spage%d.png  %spage%d.pdf ", pathpng,
            page, pathpdf, page);
        printf("command = %s \n", filenameX);
        system(filenameX);

        if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
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
    state = 1;
}

int precedingDot(int x, int y) {
    int pos = (y * pageSetWidth) + (x-1);
    if (printermemory[pos] == 128) return 0; // White
    return 1;
}

void _tiff_delta_printing(int compressMode, float hPixelWidth, float vPixelWidth) {
    int opr, xByte, j, bytePointer, colour, existingColour;
    unsigned int xd, repeater, command, dataCount;
    signed int parameter;
    unsigned char seedrow[5954 * 4];
    int moveSize = 1; // Set by MOVEXDOT or MOVEXBYTE
    if (compressMode == 3) {
        // Delta Row Compression
        for (bytePointer = 0; bytePointer < pageSetWidth * 4; bytePointer++) {
            seedrow[bytePointer]=0;
        }
    }
    existingColour = printColour;
    if (printColour > 4) printColour = 4;
    
  tiff_delta_loop:    
    // timeout
    state = 0;
    while (state == 0) {
        state = read_byte_from_printer((char *) &xd);
        if (state == 0) goto raus_tiff_delta_print;
    }
    
    // Get command into nibbles
    command = (unsigned char) xd >> 4;
    parameter = (unsigned char) xd & 0xF;
    switch (command) {
    case 2:
        // XFER 0010 xxxx - parameter number of raster image data 0...15
        for (opr = 0; opr < parameter; opr ++) {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);
                if (state == 0) goto raus_tiff_delta_print;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    state = 0;
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        if (state == 0) goto raus_tiff_delta_print;
                    }
                    for (xByte = 0; xByte < 8; xByte++) {
                        bytePointer = printColour * pageSetWidth + xpos;
                        if (xd & 128) {
                            putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                            if (compressMode == 3) {
                                // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                                if (xpos < pageSetWidth) seedrow[bytePointer] = 1;
                            }
                        } else if (compressMode == 3) {
                            // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                            if (xpos < pageSetWidth) seedrow[bytePointer] = 0;
                        }
                        xd = xd << 1;
                        xpos = xpos + hPixelWidth;
                    }
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                    opr++;
                }
            } else {
                // Repeat following byte twos complement (repeater)
                repeater = (256 - repeater) + 1;
                state = 0;               
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    if (state == 0) goto raus_tiff_delta_print;
                }
                for (j = 0; j < repeater; j++) {
                    for (xByte = 0; xByte < 8; xByte++) {
                        bytePointer = printColour * pageSetWidth + xpos;
                        if (xd & 128) {
                            putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                            if (compressMode == 3) {
                                // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                                if (xpos < pageSetWidth) seedrow[bytePointer] = 1;
                            }
                        } else if (compressMode == 3) {
                            // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                            if (xpos < pageSetWidth) seedrow[bytePointer] = 0;
                        }
                        xd = xd << 1;
                        xpos = xpos + hPixelWidth;
                    }
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                opr++;
            }
        }    
        break;
    case 3:
        // XFER 0011 xxxx - parameter number of lookups to raster printer image data 1...2
        if (parameter == 1) {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &dataCount);
                if (state == 0) goto raus_tiff_delta_print;
            }
        } else {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &nL);
                if (state == 0) goto raus_tiff_delta_print;
            }
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &nH);
                if (state == 0) goto raus_tiff_delta_print;
            }
            dataCount = nL + (256 * nH);
        }
        for (opr = 0; opr < dataCount; opr ++) {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                if (state == 0) goto raus_tiff_delta_print;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    state = 0;
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        if (state == 0) goto raus_tiff_delta_print;
                    }
                    for (xByte = 0; xByte < 8; xByte++) {
                        bytePointer = printColour * pageSetWidth + xpos;
                        if (xd & 128) {
                            putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                            if (compressMode == 3) {
                                // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                                if (xpos < pageSetWidth) seedrow[bytePointer] = 1;
                            }
                        } else if (compressMode == 3) {
                            // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                            if (xpos < pageSetWidth) seedrow[bytePointer] = 0;
                        }
                        xd = xd << 1;
                        xpos = xpos + hPixelWidth;
                    }
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                    opr++;
                }
            } else {
                // Repeat following byte twos complement (repeater)
                repeater = (256 - repeater) + 1;
                state = 0;              
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    if (state == 0) goto raus_tiff_delta_print;
                }
                for (j = 0; j < repeater; j++) {
                    for (xByte = 0; xByte < 8; xByte++) {
                        bytePointer = printColour * pageSetWidth + xpos;
                        if (xd & 128) {
                            putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                            if (compressMode == 3) {
                                // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                                if (xpos < pageSetWidth) seedrow[bytePointer] = 1;
                            }
                        } else if (compressMode == 3) {
                            // for ESC.3 Delta Row also copy bytes to seedrow for current colour
                            if (xpos < pageSetWidth) seedrow[bytePointer] = 0;
                        }
                        xd = xd << 1;
                        xpos = xpos + hPixelWidth;
                    }
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                opr++;
            }
        }    
        break; 
    case 4:
        // MOVX 0100 xxxx - space to move -8 to 7
        if (parameter > 7) parameter = 7 - parameter;
        thisDefaultUnit = defaultUnit;
        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units        
        xpos2 = xpos + parameter * moveSize * thisDefaultUnit;
        if (xpos2 >= marginleftp && xpos2 <= marginrightp) xpos = xpos2;
        break;
    case 5:
        // MOVX 0101 xxxx - parameter number of lookups to movement data 1...2
        if (parameter == 1) {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &parameter);
                if (state == 0) goto raus_tiff_delta_print;
            }
            if (parameter > 127) parameter = 127 - parameter; 
        } else {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &nL);
                if (state == 0) goto raus_tiff_delta_print;
            }
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &nH);
                if (state == 0) goto raus_tiff_delta_print;
            }
            parameter = nL + (256 * nH);
            if (parameter > 32767) parameter = 32767 - parameter;
        }
        thisDefaultUnit = defaultUnit;
        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units        
        xpos2 = xpos + parameter * moveSize * thisDefaultUnit;
        if (xpos2 >= marginleftp && xpos2 <= marginrightp) xpos = xpos2;
        break;
    case 6:
        // MOVY 0110 xxxx - space to move down 0 to 15 units
        // See ESC ( U command for unit
        thisDefaultUnit = defaultUnit;
        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
        ypos = ypos + (parameter * thisDefaultUnit);
        test_for_new_paper();
        if (compressMode == 3) {
            // Copy all seed data across to new row
            colour = printColour;
            for (printColour = 0; printColour < 5; printColour++) {
                xpos = 0;
                for (bytePointer = printColour * pageSetWidth; bytePointer < (printColour+1) * pageSetWidth; bytePointer++) {
                    if (seedrow[bytePointer] == 1) putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                    xpos = xpos + hPixelWidth;
                }
            }
            printColour = colour;
        }
        xpos = 0;
        break;
    case 7:
        // MOVY 0111 xxxx - space to move down X dots
        if (parameter == 1) {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &parameter);
                if (state == 0) goto raus_tiff_delta_print;
            } 
        } else {
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &nL);
                if (state == 0) goto raus_tiff_delta_print;
            }
            state = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &nH);
                if (state == 0) goto raus_tiff_delta_print;
            }
            parameter = nL + (256 * nH);
        }
        thisDefaultUnit = defaultUnit;
        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
        ypos = ypos + (parameter * thisDefaultUnit);
        test_for_new_paper();
        if (compressMode == 3) {
            // Copy all seed data across to new row
            colour = printColour;
            for (printColour = 0; printColour < 5; printColour++) {
                xpos = 0;
                for (bytePointer = printColour * pageSetWidth; bytePointer < (printColour+1) * pageSetWidth; bytePointer++) {
                    if (seedrow[bytePointer] == 1) putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                    xpos = xpos + hPixelWidth;
                }
            }
            printColour = colour;
        }
        xpos = 0;
        break;
    case 8:
        // COLR 1000 xxxx 
        printColour = parameter;
        break;
    case 14:
        switch (parameter) {
        case 1:
            // CLR 1110 0001
            if (compressMode == 3) {
                // Clear seedrow for current colour
                for (bytePointer = printColour * pageSetWidth; bytePointer < (printColour+1) * pageSetWidth; bytePointer++) {
                    seedrow[bytePointer] = 0;
                }
                // Reset the current row on the paper to white and then add the other colours
                bytePointer = ypos * pageSetWidth;
                for (j = 0; j < pageSetWidth; j++) printermemory[bytePointer + j] = 1 << 7;
                if (sdlon == 1) {
                    // Clear display
                    for (xpos = 0; xpos < pageSetWidth; xpos++) {
                        colour = printColour;
                        printColour = 7; // White
                        putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                        printColour = colour;
                    }
                }
                // Copy all other seed data across to row on page
                colour = printColour;
                for (printColour = 0; printColour < 5; printColour++) {
                    if (printColour != colour) {
                        xpos = 0;
                        for (bytePointer = printColour * pageSetWidth; bytePointer < (printColour+1) * pageSetWidth; bytePointer++) {
                            if (seedrow[bytePointer] == 1) putpixelbig(xpos, ypos, hPixelWidth, vPixelWidth);
                            xpos = xpos + hPixelWidth;
                        }
                    }
                }
                printColour = colour;
                xpos = 0;
            }
            break;
        case 2:
            // CR 1110 0010
            xpos = 0;
            break;
        case 3:
            // EXIT 1110 0011
            xpos = 0;
            goto raus_tiff_delta_print; 
            break;
        case 4:
            // MOVEXBYTE 1110 0100
            moveSize = 8;
            xpos = 0;
            break;
        case 5:
            // MOVEXDOT 1110 0101
            moveSize = 1;
            xpos = 0;
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
    unsigned int xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        // timeout
        state = 0;
        while (state == 0) {
            state = read_byte_from_printer((char *) &xd);  // byte1
            if (state == 0) goto raus_8p;
        }

        if ((dotColumns - opr) == 3) opr = opr; // SASCHA - what is this intended to do?
        for (fByte = 0; fByte < 8; fByte++) {
            if (xd & 128) {
                if ((adjacentDot == 0) && (precedingDot(xpos, ypos + fByte * vPixelWidth) == 1)) {
                    // Miss out second of two consecutive horizontal dots
                } else {
                    putpixelbig(xpos, ypos + fByte * vPixelWidth, hPixelWidth, vPixelWidth);
                }
            } 
            xd = xd << 1;
        }
        xpos = xpos + hPixelWidth;
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
    unsigned int xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        state = 0;
        while (state == 0) {
            state = read_byte_from_printer((char *) &xd);  // byte1
            if (state == 0) goto raus_9p;
        }
        for (xByte = 0; xByte < 8; xByte++) {
            if (xd & 128) {
                if ((adjacentDot == 0) && (precedingDot(xpos, ypos + xByte * vPixelWidth) == 1)) {
                    // Miss out second of two consecutive horizontal dots
                } else {
                    putpixelbig(xpos, ypos + xByte * vPixelWidth, hPixelWidth, vPixelWidth);
                }
            }            
            xd = xd << 1;
        }
        // Read pin 9
        state = 0;
        while (state == 0) {
            state = read_byte_from_printer((char *) &xd);  // byte2
            if (state == 0) goto raus_9p;
        }
        if (xd & 1) {
            if ((adjacentDot == 0) && (precedingDot(xpos, ypos + 9 * vPixelWidth) == 1)) {
                // Miss out second of two consecutive horizontal dots
            } else {
                putpixelbig(xpos, ypos + 9 * vPixelWidth, hPixelWidth, vPixelWidth);
            }
        }         
        xpos = xpos + hPixelWidth;
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
    unsigned int xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        // print 3 bytes (3 x 8 dots) per column
        for (fByte = 0; fByte < 3; fByte++) {
            ypos2 = ypos + fByte * (8 * vPixelWidth);
            state = 0;

            while (state == 0) {
                state = read_byte_from_printer((char *) &xd);  // byte1
                if (state == 0) goto raus_24p;
            }
            for (xByte = 0; xByte < 8; xByte++) {
                if (xd & 128) {
                    if ((adjacentDot == 0) && (precedingDot(xpos, ypos2 + xByte * vPixelWidth) == 1)) {
                        // Miss out second of two consecutive horizontal dots
                    } else {
                        putpixelbig(xpos, ypos2 + xByte * vPixelWidth, hPixelWidth, vPixelWidth);
                    }
                }                 
                xd = xd << 1;
            }
        }
        xpos = xpos + hPixelWidth;
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
    unsigned int xd;
    test_for_new_paper();
    for (opr = 0; opr < dotColumns; opr++) {
        // print 6 bytes (6 x 8 dots) per column
        for (fByte = 0; fByte < 6; fByte++) {
            ypos2 = ypos + fByte * (8 * vPixelWidth);
            state = 0;

            while (state == 0) {
                state = read_byte_from_printer((char *) &xd);  // byte1
                if (state == 0) goto raus_48p;
            }
            for (xByte = 0; xByte < 8; xByte++) {
                if (xd & 128) {
                    if ((adjacentDot == 0) && (precedingDot(xpos, ypos2 + xByte * vPixelWidth) == 1)) {
                        // Miss out second of two consecutive horizontal dots
                    } else {
                        putpixelbig(xpos, ypos2 + xByte * vPixelWidth, hPixelWidth, vPixelWidth);
                    }
                }
                xd = xd << 1;
            }
        }
        xpos = xpos + hPixelWidth;
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
    int opr, xByte, j, band;
    unsigned int xd, repeater;
    test_for_new_paper();
    for (band = 0; band < bandHeight; band++) {
        if (rleEncoded) {
            for (opr = 0; opr < dotColumns; opr++) {
                state = 0;
                while (state == 0) {
                    state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                    if (state == 0) goto raus_rasterp;
                }
                if (repeater <= 127) {
                    repeater++;
                    // string of data byes to be printed
                    for (j = 0; j < repeater; j++) {
                        state = 0;                
                        while (state == 0) {
                            state = read_byte_from_printer((char *) &xd);  // byte to be printed
                            if (state == 0) goto raus_rasterp;
                        }
                        for (xByte = 0; xByte < 8; xByte++) {
                            if (xd & 128) putpixelbig(xpos, ypos2, hPixelWidth, vPixelWidth);
                            xd = xd << 1;
                            xpos = xpos + hPixelWidth;
                        }
                        opr++;
                        // SDL_UpdateRect(display, 0, 0, 0, 0);
                    }
                } else {
                    // Repeat following byte twos complement (repeater)
                    repeater = (256 - repeater) + 1;
                    state = 0;              
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        if (state == 0) goto raus_rasterp;
                    }
                    for (j = 0; j < repeater; j++) {
                        for (xByte = 0; xByte < 8; xByte++) {
                            if (xd & 128) putpixelbig(xpos, ypos2, hPixelWidth, vPixelWidth);
                            xd = xd << 1;
                            xpos = xpos + hPixelWidth;
                        }
                        // SDL_UpdateRect(display, 0, 0, 0, 0);
                    }
                    opr++;
                }
            }        
        } else {    
            for (opr = 0; opr < dotColumns; opr++) {
                state = 0;
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte1
                    if (state == 0) goto raus_rasterp;
                }
                for (xByte = 0; xByte < 8; xByte++) {
                    if (xd & 128) putpixelbig(xpos, ypos2, hPixelWidth, vPixelWidth);
                    xd = xd << 1;
                    xpos = xpos + hPixelWidth;
                }
                // SDL_UpdateRect(display, 0, 0, 0, 0);
            }
        }
        ypos2 = ypos2 + vPixelWidth;
    }
  raus_rasterp:
    return;
}

void bitimage_graphics(int mode, int dotColumns) {
    switch (mode) {
    case 0:  // 60 x 60 dpi 9 needles
        needles = 9;
        hPixelWidth = printerdpih / (float) 60;
        vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 1:  // 120 x 60 dpi 9 needles
        needles = 9;
        hPixelWidth = printerdpih / (float) 120;
        vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 2:
        // 120 x 60 dpi 9 needles - not adjacent dot printing
        needles = 9;
        hPixelWidth = printerdpih / (float) 120;
        vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 3:
        // 240 x 60 dpi 9 needles - not adjacent dot printing 
        needles = 9;
        hPixelWidth = printerdpih / (float) 240;
        vPixelWidth = printerdpiv / (float) 60;  // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 4:  // 80 x 60 dpi 9 needles
        needles = 9;
        hPixelWidth = printerdpih / (float) 80;
        vPixelWidth = printerdpiv / (float) 60; // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 5:  // 72 x 72 dpi 9 needles - unused in ESC/P2
        needles = 9;
        hPixelWidth = printerdpih / (float) 72;
        vPixelWidth = printerdpiv / (float) 60; // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 6:  // 90 x 60 dpi 9 needles
        needles = 9;
        hPixelWidth = printerdpih / (float) 90;
        vPixelWidth = printerdpiv / (float) 60; // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 7:  // 144 x 72 dpi 9 needles (ESC/P only)
        needles = 9;
        hPixelWidth = printerdpih / (float) 144;
        vPixelWidth = printerdpiv / (float) 60; // ESCP2 definition - ESCP was 72 dpi
        _8pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 32:  // 60 x 180 dpi, 24 dots per column - row = 3 bytes
        needles = 24;
        hPixelWidth = printerdpih / (float) 60;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 33:  // 120 x 180 dpi, 24 dots per column - row = 3 bytes
        needles = 24;
        hPixelWidth = printerdpih / (float) 120;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 35:  // Resolution not verified possibly 240x216 sein
        needles = 24;
        hPixelWidth = printerdpih / (float) 240;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 38:  // 90 x 180 dpi, 24 dots per column - row = 3 bytes
        needles = 24;
        hPixelWidth = printerdpih / (float) 90;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 39:  // 180 x 180 dpi, 24 dots per column - row = 3 bytes
        needles = 24;
        hPixelWidth = printerdpih / (float) 180;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 40:  // 360 x 180 dpi, 24 dots per column - row = 3 bytes - not adjacent dot
        needles = 24;
        hPixelWidth = printerdpih / (float) 360;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _24pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 64:  // 60 x 60 dpi, 48 dots per column - row = 6 bytes
        needles = 48;
        hPixelWidth = printerdpih / (float) 60;
        vPixelWidth = printerdpiv / (float) 60;  // Das sind hier 1.2 
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 65:  // 120 x 120 dpi, 48 dots per column - row = 6 bytes
        needles = 48;
        hPixelWidth = printerdpih / (float) 120;
        vPixelWidth = printerdpiv / (float) 120;  // Das sind hier 1.2 
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 70:  // 90 x 180 dpi, 48 dots per column - row = 6 bytes
        needles = 48;
        hPixelWidth = printerdpih / (float) 90;
        vPixelWidth = printerdpiv / (float) 180;  // Das sind hier 1.2 
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 71:  // 180 x 360 dpi, 48 dots per column - row = 6 bytes
        needles = 48;
        hPixelWidth = printerdpih / (float) 180;
        vPixelWidth = printerdpiv / (float) 360;  // Das sind hier 1.2 
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    case 72:  // 360 x 360 dpi, 48 dots per column - row = 6 bytes - no adjacent dot printing
        needles = 48;
        hPixelWidth = printerdpih / (float) 360;
        vPixelWidth = printerdpiv / (float) 360;  // Das sind hier 1.2 
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
        break;
    case 73:  // 360 x 360 dpi, 48 dots per column - row = 6 bytes
        needles = 48;
        hPixelWidth = printerdpih / (float) 360;
        vPixelWidth = printerdpiv / (float) 360;  // Das sind hier 1.2 
        // Pixels
        _48pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
        break;
    }
}

// Loads a font with 16bytes per char, first 128 characters, so 2048 and not more!
// Returns 1 if ok and -1 if not OK
int openfont(char *filename)
{
    char chr = 'A';
    int i = 0;
    FILE *font;
    font = fopen(filename, "r");
    if (font == NULL) return -1;
    while (!feof(font)) {
        chr = fgetc(font);
        fontx[i] = chr;
        i++;                // printf("%d\n",i);
    }
    fclose(font);
    return 1;
}

int direction_of_char = 0;
int printcharx(unsigned char chr)
{
    unsigned int adressOfChar = 0;
    unsigned int chr2;
    int i, fByte;
    int boldoffset = 0;
    int boldoffset11= 0;
    int italiccount=0;
    unsigned char xd;
    float divisor=1;
    int yposoffset=0;
    int charHeight = 24; // 24 pin printer - characters 24 pixels high
    int fontDotWidth, fontDotHeight, character_spacing;

    chr2 = (unsigned int) chr;
    adressOfChar = chr2 << 4;  // Multiply with 16 to Get Adress
    hPixelWidth = printerdpih / (float) cdpih;
    vPixelWidth = printerdpiv / (float) cdpiv;
    character_spacing = 0;
    
    // Take into account the expected size of the font for a 24 pin printer:
    // Font size is 8 x 16:
    
    if (letterQuality == 1) {
        // LETTER QUALITY 360 x 144 dpi
        // -- uses (360 / cpi) x 24 pixel font - default is 10 cpi (36 dots), 12 cpi (10 dots), 15 cpi (24 dots)
        fontDotWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 8);
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
    if (italic==1) {
        italiccount=1;
    }
    test_for_new_paper();
    
    // SUPERSCRIPT / SUBSCRIPT 360 x 144 dpi
    // -- uses (360 / cpi) x 16 pixel font - default is 10 cpi (36 dots), 12 cpi (30 dots), 15 cpi (24 dots)    
    // NOTE : if point size = 8, then subscript/superscript are also 8 point (8/72 inches)
    // character width for proportional fonts is different to full size proportional font.
    // TO BE WRITTEN
    if (superscript==1) {
        if (multipoint_mode == 1) {
            // Use nearest to 2/3
            divisor=2.0/3.0;
            vPixelWidth=vPixelWidth*divisor;
            yposoffset=2;            
        } else {
            fontDotWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 8);
            fontDotHeight = vPixelWidth;
            yposoffset=2;
        }
    } else if (subscript==1) { 
        if (multipoint_mode == 1) {
            // Use nearest to 2/3
            divisor=2.0/3.0;
            vPixelWidth=vPixelWidth*divisor;
            yposoffset=8;            
        } else {        
            fontDotHeight = vPixelWidth;
            yposoffset=8;
        }
    }
    
    hPixelWidth = fontDotWidth;
    vPixelWidth = fontDotHeight;
    
    if ((double_width == 1) || (double_width_single_line == 1)) {
        hPixelWidth = hPixelWidth * 2;
        character_spacing = character_spacing * 2;
    }
    if (double_height == 1) {
        // If ESC w sent on first line of page does NOT affect the first line
        // Move ypos back up page to allow base line of character to remain the same
        if ((chr!=32) && (ypos >= charHeight * vPixelWidth)) {
            vPixelWidth = vPixelWidth * 2;
            yposoffset = yposoffset - (charHeight * vPixelWidth); // Height of one character at double height = 2 x 24
        }
    }
    if (quad_height == 1) {
        // Star NL-10 ENLARGE command - does NOT affect the first line
        // Move ypos back up page to allow base line of character to remain the same
        if ((chr!=32) && (ypos >= charHeight * 3 * vPixelWidth)) {
            vPixelWidth = vPixelWidth * 4;
            yposoffset = yposoffset - (charHeight * 3 * vPixelWidth); // Height of one character at quad height = 4 x 24
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

            for (fByte = 0; fByte < 8; fByte++) {
                if (xd & 128) putpixelbig(xpos + fByte * hPixelWidth+italiccount*(7-i), ypos + yposoffset + i * vPixelWidth,
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
            for (fByte = 0; fByte < 8; fByte++) {
                if (xd & 001) putpixelbig(xpos + fByte * hPixelWidth+italiccount*(7-i),ypos + yposoffset+ i * vPixelWidth,
                                hPixelWidth + boldoffset, vPixelWidth + boldoffset11);
                xd = xd >> 1;
            }
        }
    }
    // Add the actual character width
    xpos = xpos + (hPixelWidth * 8);
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
            xpos = xpos + character_spacing;
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
        fontDotWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 8);
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
                                hPixelWidth * 8, vPixelWidth);
            }
        }
    }

    // Add the actual character width
    xpos = xpos + (hPixelWidth * 8);
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
            xpos = xpos + character_spacing;
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
    if (xpos > ((pageSetWidth - 1) - hPixelWidth * 8)) {
        xpos = marginleftp;
        ypos = ypos + line_spacing;
    } 
}

void cpulimit()
{
    char y[1000];
    return;
    sprintf(y, "cpulimit -p%d  -l 90 &", getpid());
    system(y);
}

void mountusb(char *path)
{
    if (0==strcmp(path,"/usbstick'")) {
        system("mkdir /usbstick 2>>/dev/null; mount /dev/sda1 /usbstick 2>>/dev/null &");    
    }

    strcpy(pathcreator, "mkdir ");
    strcat(pathcreator, path);
    strcat(pathcreator, "raw  2>>/dev/null");
    system(pathcreator);

    strcpy(pathcreator, "mkdir ");
    strcat(pathcreator, path);
    strcat(pathcreator, "png  2>>/dev/null");
    system(pathcreator);

    strcpy(pathcreator, "mkdir ");
    strcat(pathcreator, path);
    strcat(pathcreator, "pdf  2>>/dev/null");
    system(pathcreator);
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
    if (rawispcl==1) {
        sprintf(x,"mkdir %s 2>>/dev/null",pathpcl);
        system(x);
    } else if (rawiseps==1) {
        sprintf(x,"mkdir %s 2>>/dev/null",patheps);
        system(x);    
    }
}

// Convert string to lower case
char* lowerCaseWord(char* a)
{
    int i;
    char *b=malloc(sizeof(char) * strlen(a));

    for (i = 0; i < strlen(a); i++) {
        b[i] = tolower(a[i]);
    }
    return b;
}

int convertUnixWinMac(char * source,char * destination)
{
    FILE * sourceFile, * destinationFile;
    
    int bytex,bytex1=0,bytex2=0; 

    sourceFile      = fopen(source,"r");
    destinationFile = fopen(destination,"w");
    
    if (NULL==sourceFile) {
      printf("Could not open raw file for reading ---> %s\n",source);
      return -1;
    }
    if (NULL==destinationFile){
      printf("Could not open raw file for reading ---> %s\n",destination);
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

// args[1] is the file to be load
// args[2] is the divisor to reduce SDL window size.  3 is a good value for starting
int main(int argc, char *args[])
{
    unsigned int xd = 0;
    unsigned int pixelz = 0;
    int xposold;
    int i, j, countcharz = 0;
    char *config;
    FILE *FL; 

    cpulimit();
    if (argc < 5) {
        printf
            ("Usage: ./PrinterConvert <timeout> <divisor> <font> <font_direction> <sdl> <path> \n\n");

        printf("Usage: ./PrinterConvert 4 3 font2/SIEMENS.C16 1 sdlon /home/pi/data\n \n");
        printf
            ("timeout=4          --> if no char comes in within 4 sec, printout current page\n");
        printf
            ("divisor=30         --> reduce sdl display to 30% of original size\n");
        printf("font=font2/SIEMENS.C16      --> rload this font in font memory area\n");
        printf("font_direction=1   --> 0=lsb left 1=lsb right\n");
        printf("sdl=sdlon          --> display printout in sdl window\n");
        printf("path=/home/pi/data --> store all in this directory\n");

        goto raus;
    }
    printf("\n");

    // Parse the parameters in case they override the defaults

    for (i = 0; i < argc; i++) {
        param = lowerCaseWord(args[i]); // Convert to lower case
        // Output format - Epson or HP
        if (strcmp(param,"hp") == 0) {
            rawispcl=1;
            rawiseps=0;
            printf("HP (PCL-mode) on.\n");
        } else if (strcmp(param,"epson") == 0) {
            rawispcl=0;
            rawiseps=1;
            printf("EPSON (ESC P2 mode) on.\n");
        } else {
            // get default from /root/config/emulation
            if (cfileexists(EMULATION_CONFIG)) {
                FL=fopen(EMULATION_CONFIG, "r");
                fscanf(FL, "%s", config);
                fclose(FL);
                config = lowerCaseWord(config); // Convert to lower case
                if (strcmp(config,"hp") == 0) {
                    rawispcl=1;
                    rawiseps=0;
                    printf("HP (PCL-mode) on.\n");
                } else if (strcmp(config,"epson") == 0) {
                    rawispcl=0;
                    rawiseps=1;
                    printf("EPSON (ESC P2-mode) on.\n");
                }
            }
        }
        
        // End of Line Conversions
        if (rawiseps == 1) {  // only convert if the format is epson! 
            if (strcmp(param,"unix") == 0) {
                outputFormatText=1;
                printf("Unix-mode on (LF).\n");
            } else if (strncmp(param,"windows", 7) == 0) {
                outputFormatText=2;
                printf("Windows-mode on (CR+LF).\n");
            } else if (strncmp(param,"mac", 3) == 0) {
                outputFormatText=3;
                printf("MAC-mode on (CR).\n");
            } else {
                // get default from /root/config/printer_delay
                if (cfileexists(LINEFEED_CONFIG)) {
                    FL=fopen(LINEFEED_CONFIG, "r");
                    fscanf(FL, "%s", config);
                    fclose(FL);
                    config = lowerCaseWord(config); // Convert to lower case
                    if (strcmp(config,"unix") == 0) {
                        outputFormatText=1;
                        printf("Unix-mode on (LF).\n");
                    } else if (strncmp(config,"windows", 7) == 0) {
                        outputFormatText=2;
                        printf("Windows-mode on (CR+LF).\n");
                    } else if (strncmp(config,"mac", 3) == 0) {
                        outputFormatText=3;
                        printf("MAC-mode on (CR).\n");
                    }
                }            
            }
        }
        
        if (strncmp(param, "t1=", 3) == 0) t1 = atoi(&args[i][3]);
        if (strncmp(param, "t2=", 3) == 0) t2 = atoi(&args[i][3]);
        if (strncmp(param, "t3=", 3) == 0) t3 = atoi(&args[i][3]);
        if (strncmp(param, "t4=", 3) == 0) t4 = atoi(&args[i][3]);
        if (strncmp(param, "t5=", 3) == 0) t5 = atoi(&args[i][3]);

        if (strncmp(args[i], "B_b_A_a", strlen(args[i])) == 0) {
            printf ("Normal busy_on busy_off ack_on ack_off handshaking\n");
            ackposition=0;
        } else if (strncmp(args[i], "A_B_a_b", strlen(args[i])) == 0) {
            printf ("Special ack_on busy_on ack_off busy_off handshaking\n");
            ackposition=1;
        } else if (strncmp(args[i], "B_A_b_a", strlen(args[i])) == 0) {
            printf ("Special busy_on ack_on busy_off ack_off handshaking\n");
            ackposition=2;
        } else if (strncmp(args[i], "A_a_B_b", strlen(args[i])) == 0) {
            printf ("Special ack_on ack_off busy_on busy_off handshaking\n");
            ackposition=3;
        } else if (strncmp(args[i], "B_A_a_b", strlen(args[i])) == 0) {
            printf ("Special busy_on ack_on ack_off busy_off handshaking\n");
            ackposition=4;
        }
    }
    
    printf("delays around ack: t1=%d    t2=%d    t3=%d    t4=%d    t5=%d\n",t1,t2,t3,t4,t5);
    
    // Grab the path offset
    strcpy(path, args[6]);

    if (path[0] != '/') {
        // get default from /root/config/output_path
        if (cfileexists(PATH_CONFIG)) {
            FL=fopen(PATH_CONFIG, "r");
            fscanf(FL, "%s", path);
            fclose(FL);
        }        
    }
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
        strcpy(pathraw,   path);
        strcpy(pathpng, path);
        strcpy(pathpcl, path);
        strcpy(patheps, path);
    }

    strcpy(pathpdf,    path);
    strcat(pathraw,   "raw/");
    strcat(pathpng,   "png/");
    strcat(pathpdf,   "pdf/");
    strcat(pathpcl,   "pcl/");
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
    initialize();

    openfont(args[3]);
    direction_of_char = atoi(args[4]);
    if (argc >= 6) {
        if (strcmp(args[5], "sdloff") == 0) {
            sdlon = 0;
        }
    }

    divi = 100 / atof(args[2]);
    timeout = 0;
    timeout = atof(args[1]);

    if (sdlon) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            printf("Error on SDL init\n");
            exit(1);
        }
    }

    // Set the video mode
    xdim = pageSetWidth / divi;
    ydim = pageSetHeight / divi;
    if (sdlon) {
        display = SDL_SetVideoMode(xdim, ydim, 24, SDL_HWSURFACE);      
        if (display == NULL) {
            printf("Error on SDL display mode setting\n");
            exit(1);
        }
        // Set the title bar
        SDL_WM_SetCaption("Printer Output", "Printer Output");
            
        // SDL_UpdateRect (display, 0, 0, 0, 0);
        erasesdl();
        SDL_UpdateRect(display, 0, 0, 0, 0);
    }
    
main_loop_for_printing:
    mountusb(path);
    xpos = marginleftp;
    ypos = 0;
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
        state = read_byte_from_printer((char *) &xd);
        if (state == 0) {
            if (timeout > 0)
            sleep(timeout - 1);
            else
            sleep(1);
            break;
        }
        fflush(stdout);
        i++;
        if (rawispcl == 1) {
            // HP PCL printer code handling
            // Not implemented
        } else if (graphics_mode ==1) {
            // Epson ESC/P2 graphics mode - limited commands:
            switch (xd) {
            case 10:    // lf (0x0a)              
                ypos = ypos + line_spacing;
                xpos = marginleftp;
                double_width_single_line = 0;
                test_for_new_paper();
                break;
            case 12:    // form feed (neues blatt) 
                ypos = pageSetHeight;  // just put it in an out of area position
                test_for_new_paper();
                i = 0;
                double_width_single_line = 0;
                break;
            case 13:    // cr (0x0d)
                xpos = marginleftp;
                break;
            case 27:    // ESC Escape (do nothing, will be processed later in this code)
                break;                    
            }
            // ESc Branch for graphics mode
            if (xd == (int) 27) {   // ESC v 27 v 1b
                state = read_byte_from_printer((char *) &xd);

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
                    graphics_mode          =   0;
                    microweave_printing    =   0;
                    vTabulatorsSet         =   0;
                    vTabulatorsCancelled   =   0;                    
                    break;                       
                case '.':    
                    // Raster printing ESC . c v h m nL nH d1 d2 . . . dk print bit-image graphics.
                    state = read_byte_from_printer((char *) &c);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &v);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &h);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &m);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
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
                    state = read_byte_from_printer((char *) &xd);
                    line_spacing = printerdpiv * ((float) xd / (float) 360);                   
                    break; 
                case '(':    
                    state = read_byte_from_printer((char *) &nL);
                    switch (nL) {
                    case 'C': 
                        // 27 40 67 nL nH mL mH Set page length in defined unit
                        // page size not implemented yet
                        state = read_byte_from_printer((char *) &nL); // always 2
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        // pageLength = ((mH * 256) + mL) * thisDefaultUnit;
                        ypos = 0;
                        margintopp = 0;
                        marginbottomp = 8417;
                        break;
                    case 'c': 
                        // 27 40 67 nL nH tL tH bL bH Set page format
                        // top and bottom margins not implemented yet
                        state = read_byte_from_printer((char *) &nL); // always 4 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        margintopp = ((tH * 256) + tL) * thisDefaultUnit;
                        marginbottomp = ((bH * 256) + bL) * thisDefaultUnit;
                        if (marginbottomp > 22 * printerdpih) marginbottomp = 22 * printerdpih; // Max 22 inches
                        ypos = 0;
                        // cancel top and bottom margins (margintopp and marginbottomp - to be implemented)                            
                        break;
                    case 'V': 
                        // 27 40 86 nL nH mL mH Set absolute vertical print position
                        state = read_byte_from_printer((char *) &nL); // Always 2 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // Always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        ypos2 = margintopp + ((mH * 256) + mL) * thisDefaultUnit;
                        // Ignore if movement is more than 179/360" upwards
                        if (ypos2 < (ypos - (printerdpiv * ((float) 179/(float) 360))) ) ypos2 = ypos;
                        // Ignore if command would move upwards after graphics command sent on current line, or above where graphics have 
                        // previously been printed - to be written.
                        ypos = ypos2;
                        test_for_new_paper();
                        break;
                    case 'v': 
                        // 27 40 118 nL nH mL mH Set relative vertical print position
                        state = read_byte_from_printer((char *) &nL); // Always 2 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // Always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        if (mH > 127) {
                            // Handle negative movement
                            mH = 127 - mH;
                        }
                        ypos2 = ypos + ((mH * 256) + mL) * thisDefaultUnit;
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
                        state = read_byte_from_printer((char *) &nL); // Always 1 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // Always 0 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m);
                        defaultUnit = ((float) m / (float) 3600) * printerdpiv; // set default unit to m/3600 inches
                        break;
                    case 'i': 
                        // ESC ( i 01 00 n  Select microweave print mode
                        state = read_byte_from_printer((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m);
                        if ((nL == 1) && (nH == 0) ) {
                            if ((m == 0) || (m == 48)) microweave_printing = 0;
                            if ((m == 1) || (m == 49)) microweave_printing = 1;
                        }
                        break;
                    }
                    break;
                case '$':    // Set absolute horizontal print position ESC $ nL nH
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    thisDefaultUnit = defaultUnit;
                    if (defaultUnit == 0) thisDefaultUnit = printerdpih / (float) 60; // Default for command is 1/180 inch units in LQ mode
                    xpos2 = ((nH * 256) + nL) * thisDefaultUnit + marginleftp;
                    if (xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }                        
                    break;
                case 92:   // Set relative horizonal print position ESC \ nL nH
                    state = read_byte_from_printer((char *) &nL); // always 2
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH); // always 0
                    if (state == 0) break;
                    thisDefaultUnit = defaultUnit;
                    if (defaultUnit == 0) {
                        if (letterQuality == 1) {
                            thisDefaultUnit = printerdpih / (float) 180; // Default for command is 1/180 inch units in LQ mode
                        } else {
                            thisDefaultUnit = printerdpih / (float) 120; // Default for command is 1/120 inch units in draft mode
                        }
                    }
                    if (nH > 127) {
                        // Handle negative movement
                        nH = 127 - nH;
                    }
                    xpos2 = xpos + ((nH * 256) + nL) * thisDefaultUnit;
                    if (xpos2 < marginleftp || xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }
                    break;
                case 'r':   
                    // ESC r n Set printing colour (n=0-7) 
                    // (Black, Magenta, Cyan, Violet, Yellow, Red, Green, White)
                    state = read_byte_from_printer((char *) &printColour);
                    break;
                case 25:    // ESC EM n Control paper loading / ejecting (do nothing)
                    state = read_byte_from_printer((char *) &nL);
                    break;
                }
            }
            
        } else if ((print_uppercontrolcodes==1) && (xd >= (int) 128) && (xd <= (int) 159)) {
            print_character(xd);
        } else {
            // Epson ESC/P2 printer code handling
            switch (xd) {
            case 0:    // NULL do nothing
                if (print_controlcodes) {
                    print_character(xd);
                }
                break;
            case 1:    // SOH do nothing
                break;
            case 2:    // STX do nothing
                break;
            case 3:    // ETX do nothing
                break;
            case 4:    // EOT do nothing
                break;
            case 5:    // ENQ do nothing
                break;
            case 6:    // ACK do nothing
                break;
            case 7:    // BEL do nothing
                if (print_controlcodes) {
                    print_character(xd);
                }                
                break;
            case 8:    // BS (BackSpace)
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    xposold = xpos;
                    hPixelWidth = printerdpih / (float) cdpih;
                    if (letterQuality == 1) {
                        hPixelWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 8);
                    } else {
                        hPixelWidth = (float) hPixelWidth * (((float) 120 / (float) cpi) / (float) 8);
                    }
                    xpos = xpos - hPixelWidth * 8;
                    if (xpos < 0) xpos = xposold;
                }
                break;
            case 9:    // TAB
                if (print_controlcodes) {
                    print_character(xd);
                } else {
                    curHtab = -1;
                    for (i = 0; i < 32; i++) {
                        if (marginleftp + hTabulators[i] <= xpos) curHtab = i;
                    }
                    curHtab++;                    
                    if (curHtab > 31 || hTabulators[curHtab] == 0) {
                        // No more tab marks
                    } else if (hTabulators[curHtab] > 0 && hTabulators[curHtab] <= marginrightp) {
                        // forward to next tab position
                        xpos = marginleftp + hTabulators[curHtab];
                    }
                }
                break;
            case 10:    // lf (0x0a)
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    // 01.01.2014 forward 31/216 inches this may be wrong or adapted .....
                    // 27.09.2014 maybe next line is to shift down 8/72 inches to begin a new line
                    // to avoid overwritings if needles are having 72dpi mechanical resolution
                    // dpiv=216
                    // vPixelWidth=((float)dpiv/(float)cpi*2)/16; //CPI * 2 weil printcharx
                    // ebenfalls cpi * 2 nimmt
                    // ypos=ypos+vPixelWidth*16; //24=8*216/72
                    // 07.01.2015 wieder auf 24 gesetzt da hier die besten ergebnisse (was
                    // an der alten formel falschist I dunno)
                    // changed from 24 to 30 20.09.2015
                    // Those are 8 Pixels forward concerning the printheads 72dpi resolution. 
                    // Cause the mechanics has a resolution of 216 dpi (which is 3 times
                    // higher) 8x3=24 steps/pixels has to be done
                    ypos = ypos + line_spacing;
                    xpos = marginleftp;
                    double_width_single_line = 0;
                    test_for_new_paper();
                    break;
                }
                break;
            case 11:    // VT vertical tab (same like 10)
                if (print_controlcodes) {
                    print_character(xd);
                } else {
                    xpos = marginleftp;
                    curVtab = -1;
                    for (i = (vFUChannel * 16); i < (vFUChannel * 16) + 16; i++) {
                        if (vTabulators[i] <= ypos) curVtab = i;
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
                            ypos = pageSetHeight;  // just put it in an out of area position
                            test_for_new_paper();
                            i = 0;
                            double_width_single_line = 0;                                
                        } else {
                            // LF
                            curVtab = 0; // No more tab marks
                            ypos = ypos + line_spacing;
                            double_width_single_line = 0;
                        }
                    } else if (vTabulators[curVtab] > 0) {
                        // forward to next tab position
                        // ignore IF print position would be moved to inside the bottom margin
                        ypos2 = ypos;
                        ypos = vTabulators[curVtab];
                        if (ypos > (pageSetHeight - 17 * vPixelWidth)) {
                            // Do nothing
                            ypos = ypos2;
                        } else {
                            double_width_single_line = 0;
                        }
                    }
                }
                break;
            case 12:    // form feed (neues blatt) 
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    ypos = pageSetHeight;  // just put it in an out of area position
                    test_for_new_paper();
                    i = 0;
                    double_width_single_line = 0;
                }
                break;
            case 13:    // cr (0x0d)
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    xpos = marginleftp;
                }
                break;
            case 14:    // SO Shift Out (do nothing) Select double Width printing (for one line)
                if (print_controlcodes) {
                    print_character(xd);
                } else {
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (multipoint_mode == 0) double_width_single_line = 1;
                }
                break;
            case 15:    // SI Shift In (do nothing) Condensed printing on
                if (print_controlcodes) {
                    print_character(xd);
                } else {
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (multipoint_mode == 0) {                
                        if (pitch==10) cpi=17.14;
                        if (pitch==12) cpi=20;
                        // Add for proportional font = 1/2 width - to be written
                    }
                }
                break;
            case 16:    // DLE Data Link Escape (do nothing)
                break;
            case 17:    // DC1 (Device Control 1)
                // Intended to turn on or start an ancillary device, to restore it to
                // the basic operation mode (see DC2 and DC3), or for any
                // other device control function.
                if (print_controlcodes) {
                    print_character(xd);
                }                    
                break;
            case 18:    // DC2 (Device Control 2) Condensed printing off, see 15
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (pitch==10) cpi=10;
                    if (pitch==12) cpi=12;
                    // Add for proportional font = full width
                }
                break;
            case 19:    // DC3 (Device Control 3)                
                // Intended for turning off or stopping an ancillary device. It may be a 
                // secondary level stop such as wait, pause, 
                // stand-by or halt (restored via DC1). Can also perform any other
                // device control function.
                if (print_controlcodes) {
                    print_character(xd);
                }                    
                break;
            case 20:    // DC4 (Device Control 4)
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    // Intended to turn off, stop or interrupt an ancillary device, or for
                    // any other device control function.
                    // Also turns off double-width printing for one line
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    double_width_single_line = 0;
                }
                break;
            case 21:    // NAK Negative Acknowledgement (do nothing)
                break;
            case 22:    // Syn Synchronus idle (do nothing)
                break;
            case 23:    // ETB End Of Transmition Block (do nothing)
                break;
            case 24:    // CAN Cancel (do nothing)
                // Not implemented - normally wipes the current line of all characters and graphics
                if (print_controlcodes) {
                    print_character(xd);
                } else {
                    xpos = marginleftp;
                }
                break;
            case 25:    // EM End Of Medium (do nothing)
                if (print_controlcodes) {
                    print_character(xd);
                }                
                break;
            case 26:    // SUB Substitute (do nothing)
                break;
            case 27:    // ESC Escape (do nothing, will be processed later in this code)
                if (print_controlcodes) {
                    print_character(xd);
                }                
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
                    hPixelWidth = (float) hPixelWidth * (((float) 360 / (float) cpi) / (float) 8);
                    vPixelWidth = (float) vPixelWidth * ((float) charHeight / (float) 16);
                } else {
                    hPixelWidth = (float) hPixelWidth * (((float) 120 / (float) cpi) / (float) 8);
                    vPixelWidth = (float) vPixelWidth * ((float) charHeight / (float) 16);
                }                
                xpos = xpos + hPixelWidth * 8;
                if (xpos > ((pageSetWidth - 1) - vPixelWidth * 16)) {
                    xpos = marginleftp;
                    ypos = ypos + vPixelWidth * 16;
                }
                break;
            default:
                print_character(xd);
                break;
            }

            // ESc Branch
            if ((xd == (int) 27) && (print_controlcodes == 0) ) {   // ESC v 27 v 1b
                state = read_byte_from_printer((char *) &xd);

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
                    vTabulatorsSet         =   0;
                    vTabulatorsCancelled   =   0;
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
                case '3':    // Set n/180-inch line spacing ESC 3 n
                    state = read_byte_from_printer((char *) &xd);
                    line_spacing = printerdpiv * ((float) xd / (float) 180);
                    break;                        
                case '+':    // Set n/360-inch line spacing ESC + n
                    state = read_byte_from_printer((char *) &xd);
                    line_spacing = printerdpiv * ((float) xd / (float) 360);
                    break;                        
                case 'A':    // ESC A n Set n/60 inches (24 pin printer - ESC/P2 or ESC/P) or
                    // n/72 inches line spacing (9 pin printer)
                    state = read_byte_from_printer((char *) &xd);
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
                    state = read_byte_from_printer((char *) &xd);
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
                        state = read_byte_from_printer((char *) &xd);
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
                        state = read_byte_from_printer((char *) &xd);
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
                    state = read_byte_from_printer((char *) &m);
                    while ((xd != 0) && (i < 16)) {
                        // maximum 16 vertical tabs are allowed in each VFU Channel
                        // Last tab is always 0 to finish list
                        state = read_byte_from_printer((char *) &xd);
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
                    state = read_byte_from_printer((char *) &m);
                    vFUChannel = m;
                    break;                        
                case 'e':    // ESC e m n, set fixed tab increment
                    state = read_byte_from_printer((char *) &m);
                    state = read_byte_from_printer((char *) &nL);
                    int step = nL;
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
                    state = read_byte_from_printer((char *) &nL);
                    break;                         
                case 'Q':    // ESC Q m set the right margin
                    state = read_byte_from_printer((char *) &xd);
                    marginright = (int) xd;
                    marginrightp = (printerdpih / (float) cpi) * (float) marginright;  // rand in pixels
                    // von links
                    break;
                case 'J':    // ESC J m Forward paper feed m/180 inches (ESC/P2)
                    state = read_byte_from_printer((char *) &xd);
                    ypos = ypos + (float) xd * (printerdpiv / (float) 180);
                    test_for_new_paper();
                    break;
                case '?':    // ESC ? n m re-assign bit image mode
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &m);
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
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escKbitDensity, dotColumns);
                    // if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
                    break;
                case 'L':    // ESC L nL nH d1 d2...dk Select 120 dpi graphics
                    needles = 9;
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escLbitDensity, dotColumns);
                    break;
                case 'Y':    // ESC Y nL nH d1 d2...dk Select 120 dpi double-speed graphics
                    // Adjacent printing disabled
                    needles = 9; 
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escYbitDensity, dotColumns);
                    break;                        
                case 'Z':    // ESC Z nL nH d1 d2...dk Select 240 dpi graphics
                    needles = 9;
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    bitimage_graphics(escZbitDensity, dotColumns);
                    break;
                case '^': 
                    // ESC ^ m nL nH d1 d2 d3 d...  Select 60 oe 120 dpi, 9 pin graphics
                    state = read_byte_from_printer((char *) &m);
                    if (state == 0) break;                        
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH); 
                    if (state == 0) break;
                    hPixelWidth = printerdpih / (float) 60;
                    if (m == 1) hPixelWidth = printerdpih / (float) 120;
                    vPixelWidth = printerdpiv / (float) 72;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    _9pin_line_bitmap_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
                    break;
                case 'V':    // ESC V n d1 d2 d3 d....  Repeat data n number of times
                    // Data allows up to 2047 extra bytes - last byte is identified with ESC V NUL
                    // Not currently supported - only used by LQ-1500 and SQ-2000 printers
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    // if (nL == 0) bufferData = 0;
                    break;                     
                case 'S':    // ESC S n select superscript/subscript printing
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
                    if ((nL==1) || (nL==49)) underlined=1;
                    if ((nL==0) || (nL==48)) underlined=0; 
                    break;
                case 'W':    // ESC W SELECT DOUBLE WIDTH
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
                    switch (nL) {
                    case '-':
                        // ESC ( - nl nh m d1 d2  Select line/score
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *)  &m); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &d1); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &d2); 
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

                            printf("underlined  %d\n",underlined);
                            printf("strikethrough  %d\n",strikethrough);
                            printf("overscore  %d\n",overscore);
                        }
                        break;
                    case 'C': 
                        // 27 40 67 nL nH mL mH Set page length in defined unit
                        // page size not implemented yet
                        state = read_byte_from_printer((char *) &nL); // always 2
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        // pageLength = ((mH * 256) + mL) * thisDefaultUnit;
                        ypos = 0;
                        margintopp = 0;
                        marginbottomp = 8417;
                        break;
                    case 'c': 
                        // 27 40 67 nL nH tL tH bL bH Set page format
                        // top and bottom margins not implemented yet
                        state = read_byte_from_printer((char *) &nL); // always 4 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        margintopp = ((tH * 256) + tL) * thisDefaultUnit;
                        marginbottomp = ((bH * 256) + bL) * thisDefaultUnit;
                        if (marginbottomp > 22 * printerdpih) marginbottomp = 22 * printerdpih; // Max 22 inches
                        ypos = 0;
                        // cancel top and bottom margins (margintopp and marginbottomp - to be implemented)                            
                        break;
                    case 'V': 
                        // 27 40 86 nL nH mL mH Set absolute vertical print position
                        state = read_byte_from_printer((char *) &nL); // Always 2 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // Always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH);
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        ypos2 = margintopp + ((mH * 256) + mL) * thisDefaultUnit;
                        // Ignore if movement is more than 179/360" upwards
                        if (ypos2 < (ypos - (printerdpiv * ((float) 179/(float) 360))) ) ypos2 = ypos;
                        // Ignore if command would move upwards after graphics command sent on current line, or above where graphics have 
                        // previously been printed - to be implemented
                        ypos = ypos2;
                        test_for_new_paper();
                        break;
                    case 'v': 
                        // 27 40 118 nL nH mL mH Set relative vertical print position
                        state = read_byte_from_printer((char *) &nL); // Always 2 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // Always 0
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        thisDefaultUnit = defaultUnit;
                        if (defaultUnit == 0) thisDefaultUnit = printerdpiv / (float) 360; // Default for command is 1/360 inch units
                        if (mH > 127) {
                            // Handle negative movement
                            mH = 127 - mH;
                        }
                        ypos2 = ypos + ((mH * 256) + mL) * thisDefaultUnit;
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
                        state = read_byte_from_printer((char *) &nL); // Always 1 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); // Always 0 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m);
                        defaultUnit = ((float) m / (float) 3600) * printerdpiv; // set default unit to m/3600 inches
                        break;
                    case 'i': 
                        // ESC ( i 01 00 n  Select microweave print mode
                        state = read_byte_from_printer((char *) &nL);
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH);
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m);
                        if ((nL == 1) && (nH == 0) ) {
                            if ((m == 0) || (m == 48)) microweave_printing = 0;
                            if ((m == 1) || (m == 49)) microweave_printing = 1;
                        }
                        break;                        
                    case 't': 
                        // 27 40 116 nL nH d1 d2 d3 Assign character table
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &d1); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &d2); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &d3); 
                        break;
                    case '^': 
                        // ESC ( ^ nL nH d1 d2 d3 d...  print data as characters (ignore control codes)
                        state = read_byte_from_printer((char *) &nL); // NULL 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        m = (nH * 256) + nL;
                        j = 0;
                        while (j < m) {
                            // read data and print as normal 
                            // ignore if no character assigned to that character code in current character table
                            state = read_byte_from_printer((char *) &xd); 
                            print_character(xd);
                            j++;
                        }
                        break;                        
                    case 'G': 
                        // ESC ( G nL nH m  Select graphics mode
                        state = read_byte_from_printer((char *) &nL); // NULL 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m); 
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
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'R':
                    // ESC R n Select international character set
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case '&': 
                    // ESC & NULL nL m a0 a1 a2 d1 d2 d3 d...  Define user defined characters
                    // not implemented yet - Needs more work because number of d1.... codes varies and not clear how to calculate!!
                    state = read_byte_from_printer((char *) &nL); // NULL 
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nL); 
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &m); 
                    if (state == 0) break;
                    if (m < nL) break;
                    i = 0;
                    if (needles == 9) {
                        // ESC & NULL nL m a0 d1 d2 d3 d... for draft
                        // ESC & NULL nL m 0 a0 0 d1 d2 d3 d... for NLQ
                        state = read_byte_from_printer((char *) &a0); 
                        if (state == 0) break;
                        if (a0 == 0) {
                            // NLQ
                            while (i < (m-nL)+1) {
                                state = read_byte_from_printer((char *) &a0); 
                                if (state == 0) break;
                                state = read_byte_from_printer((char *) &xd); 
                                if (state == 0) break;
                                // Should be 0 returned
                                j = 0;
                                // Check - should this be a0
                                while (j < a1 * 3) {
                                    state = read_byte_from_printer((char *) &xd);
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
                                state = read_byte_from_printer((char *) &xd);
                                if (state == 0) break;
                                j++;
                            }
                            i++;
                            while (i < (m-nL)+1) {
                                state = read_byte_from_printer((char *) &a0); 
                                if (state == 0) break;
                                j = 0;
                                // Check - should this be a0
                                while (j < a1 * 3) {
                                    state = read_byte_from_printer((char *) &xd);
                                    if (state == 0) break;
                                    j++;
                                }
                                i++;
                            }
                        }
                    } else {  // needles must be 24 here
                        // ESC & NULL nL m a0 a1 a2 d1 d2 d3 d...
                        while (i < (m-nL)+1) {
                            state = read_byte_from_printer((char *) &a0); 
                            if (state == 0) break;
                            state = read_byte_from_printer((char *) &a1); 
                            if (state == 0) break;
                            state = read_byte_from_printer((char *) &a2);
                            if (state == 0) break;
                            j = 0;
                            // Check - we are not using a0 or a2
                            while (j < a1 * 3) {
                                state = read_byte_from_printer((char *) &xd);
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
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case '%':
                    // ESC % n Select user-defined character set
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    break;                                                
                case 'k':
                    // ESC k n Select typeface for LQ printing - ignore id user-defined character set selected
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    break;                        
                case 'f':
                    // ESC f m n Horizontal / Vertical Skip
                    state = read_byte_from_printer((char *) &m);
                    state = read_byte_from_printer((char *) &nL);
                    if (m == 0) {
                        // print nL spaces in current pitch - use underline if set
                        int k;
                        for (k = 0; k < nL; k++) print_space(1);
                    } else {
                        // perform nL line feeds in current line spacing cancel double width printing set with SO or ESC SO
                        ypos = ypos + (nL * line_spacing);
                        xpos = marginleftp;
                        double_width_single_line = 0;
                        test_for_new_paper();
                    }
                    break;
                case 'j':
                    // ESC j n Reverse paper feed n/216 inches
                    state = read_byte_from_printer((char *) &nL);
                    ypos = ypos - ((float) 720 * ((float) nL /(float) 216));
                    if (ypos < 0) ypos = 0;
                    break;
                case 'c':
                    // ESC c nL nH Set Horizonal Motion Index (HMI)
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nH);
                    hmi = printerdpih * ((float) (nH *256) + (float) nL / (float) 360);
                    break;                        
                case 'X':
                    // ESC X m nL nH Select font by pitch & point
                    // not implemented yet
                    multipoint_mode = 1;
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    state = read_byte_from_printer((char *) &m);
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nH);
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
                    state = read_byte_from_printer((char *) &nL);
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
                    state = read_byte_from_printer((char *) &nL);
                    break;                    
                case 's':    // Select low-speed mode on/off ESC s n
                    // not required
                    state = read_byte_from_printer((char *) &nL);
                    break;                    
                case 'C':    // 27 67 n Set page length in lines 1<n<127
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    if (nL == 0) {  // 27 67 0 n Set page length in inches 1<n<22
                        state = read_byte_from_printer((char *) &nL);
                    }
                    break;
                case 'N':    // Set bottom margin ESC N n 0<n<=127 top-of-form
                    // position on the next page
                    // not implemented (continuous paper only) - ignored when printing on single sheets// not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'O':    // Cancel bottom margin Cancels the top and bottom
                    // margin settings
                    margintopp = 0;
                    marginbottomp = 8417;
                    break;
                case '$':    // Set absolute horizontal print position ESC $ nL nH
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    thisDefaultUnit = defaultUnit;
                    if (defaultUnit == 0) thisDefaultUnit = printerdpih / (float) 60; // Default for command is 1/180 inch units in LQ mode
                    xpos2 = ((nH * 256) + nL) * thisDefaultUnit + marginleftp;
                    if (xpos2 > marginrightp) {
                        // No action
                    } else {
                        xpos = xpos2;
                    }                        
                    break;
                case 92:   // Set relative horizonal print position ESC \ nL nH
                    state = read_byte_from_printer((char *) &nL); // always 2
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH); // always 0
                    if (state == 0) break;
                    thisDefaultUnit = defaultUnit;
                    if (defaultUnit == 0) {
                        if (letterQuality == 1) {
                            thisDefaultUnit = printerdpih / (float) 180; // Default for command is 1/180 inch units in LQ mode
                        } else {
                            thisDefaultUnit = printerdpih / (float) 120; // Default for command is 1/120 inch units in draft mode
                        }
                    }
                    if (nH > 127) {
                        // Handle negative movement
                        nH = 127 - nH;
                    }
                    xpos2 = xpos + ((nH * 256) + nL) * thisDefaultUnit;
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
                    state = read_byte_from_printer((char *) &nL);
                    if (italic == 0 ) {
                        if (nL==1) print_controlcodes = 1;
                        if (nL==0) print_controlcodes = 0;
                    }
                    break;
                case 'm':    // ESC m n - Select printing of upper control codes
                    state = read_byte_from_printer((char *) &nL);
                    if (italic == 0 ) {
                        if (nL==0) print_uppercontrolcodes=1;
                        if (nL==4) print_uppercontrolcodes=0;                     
                    }
                    break;
                case 'r':   
                    // ESC r n Set printing colour (n=0-7) 
                    // (Black, Magenta, Cyan, Violet, Yellow, Red, Green, White)
                    state = read_byte_from_printer((char *) &printColour);
                    break;
                case 10: // ESC LF 
                    // Reverse line feed - Star NL-10
                    ypos = ypos - line_spacing;
                    xpos = marginleftp;
                    double_width_single_line = 0;    
                    break;
                case 12: // ESC FF
                    // Reverse form feed - Star NL-10
                    ypos = 0;  // just put it in an out of area position
                    xpos = marginleftp;                      
                    break;                        
                case 20:    // ESC SP Set intercharacter space
                    state = read_byte_from_printer((char *) &nL);
                    hmi = printerdpih * ((float) 36 / (float) 360); // Reset HMI
                    if (multipoint_mode == 0) {
                        chrSpacing = nL;
                    }                    
                    break;                        
                case 25:    // ESC EM n Control paper loading / ejecting (do nothing)
                    state = read_byte_from_printer((char *) &nL);
                    break;                       
                case '*':    // ESC * m nL nH d1 d2 . . . dk print bit-image graphics.
                    m = 0;
                    nL = 0;
                    nH = 0;
                    state = read_byte_from_printer((char *) &m);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
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
    if (i == 0) goto main_loop_for_printing;
   
    printf("\n\nI am at page %d\n", page);

    sprintf(filenameX, "%spage%d.png", pathpng, page);
    printf("write   = %s \n", filenameX);
    write_png(filenameX, pageSetWidth, pageSetHeight, printermemory);
    
    if (rawispcl == 1) {
        sprintf(filenameX, "cp  %s*.raw  %s ", pathraw,pathpcl);
        printf("command = %s \n", filenameX);
        system(filenameX);

        // SASCHA - not sure why we need this bit below - we have copied the raw page to the pcl path - so what is following doing - is it deleting the raw files?
        sprintf(filenameX, "for i in %s*.raw;do mv $i $i.pcl;done", pathpcl,pathpcl,pathpcl);
        printf("command = %s \n", filenameX);
        system(filenameX);
    } else if (rawiseps == 1) {
        if (outputFormatText == 0) {
            // No end of line conversion required
            sprintf(filenameX, "cp  %s*.raw  %s ", pathraw,patheps);
            printf("command = %s \n", filenameX);
            system(filenameX);
        } else {
            sprintf(filenameX, "%s%d.raw", pathraw,page);
            sprintf(filenameY, "%s%d.eps", patheps,page);
            convertUnixWinMac(filenameX,filenameY);
        }
    }     

    sprintf(filenameX, "convert  %spage%d.png  %spage%d.pdf ", pathpng, page,pathpdf, page);
    printf("command = %s \n", filenameX);
    system(filenameX);

    system("sync &"); //avoid loss of data if usb-stick is pulled off   
 
    if ((fp != NULL)) {
        fclose(fp);
        fp = NULL;
    }        // close text file
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
    
    // No more data to be read from file
    if (feof(inputFile)) {
        fclose(inputFile);
        free(printermemory);
        exit(0);
    }    
    
    // sleep(1);
    goto main_loop_for_printing;

  raus:
    return 0;
}
