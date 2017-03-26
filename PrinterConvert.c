#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "SDL.h"
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

unsigned int page = 0;
char filenameX[1000];
char filenameY[1000];
char *param;                // parameters passed to program
int xdim, ydim;
unsigned char printermemory[1984 * 2525 * 3];
int sdlon = 1;              // sdlon=0 Do not copy output to SDL screen
int state = 1;              // State of the centronics interface
int timeout = 4;            // printout finished after this time. so start to print all received data.

char    path[1000];         // main path 
char    pathraw[1000];      //path to raw files
char    pathbmp[1000];      //path to bmp files
char    pathpdf[1000];      //path to pdf files
char    pathpcl[1000];      //path to pcl files if rawispcl = 1
char    patheps[1000];      //path to eps files if rawiseps = 1
char    pathcreator[1000];  //path to usb

struct timespec tvx;
int startzeit;              // start time for time measures to avoid infinite loops

int t1=0,t2=0,t3=0,t4=0,t5=0;
int offlineswitch          = 0;
int ackposition            = 0;
int rawispcl               = 0;         //if 1 the raw folder is copied to pcl folder and raw files are renamed to *.pcl in the pcl folder
int rawiseps               = 0;         //if 1 the raw folder is copied to eps folder and raw files are renamed to *.eps in the eps folder
int outputFormatText       = 0;         //0=no conversion  1=Unix (LF) 2= Windows (CR+LF) 3=MAC (CR)
int printColour            = 0;         //Default colour is black    

int bold                   = 0;         //Currently bold and double-strike are the same
int italic                 = 0;
int underlined             = 0;
int superscript            = 0;
int subscript              = 0;
int strikethrough          = 0;
int overscore              = 0;
int single_continuous_line = 0;
int double_continuous_line = 0;
int single_broken_line     = 0;
int double_broken_line     = 0;
int double_width           = 0;         //Double width printing not yet implemented
int double_height          = 0;         //Double height printing not yet implemented - NB does not affect first line of a page!
int outline_printing       = 0;         //Outline printing not yet implemeneted
int shadow_printing        = 0;         //Shadow printing not yet implemented

int print_controlcodes     = 0;
int print_uppercontrolcodes  = 0;

int graphics_mode          = 0;
int microweave_printing    = 0;

int read_byte_from_printer(unsigned char *bytex)
{
    /* This routine needs to be written according to your requirements
    * the routine needs to fetch the next byte from the captured printer data file (or port).
    */

    *bytex = databyte;
    return 1;
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

bool isNthBitSet (unsigned char c, int n) {
  return (1 & (c >> n));
}

struct BMPHeader {
    char bfType[2];         // "BM"
    int bfSize;             // Size of file in bytes
    int bfReserved;         // set to 0
    int bfOffBits;          // Byte offset to actual bitmap data (= 54)
    int biSize;             // Size of BITMAPINFOHEADER, in bytes (= 40)
    int biWidth;            // Width of image, in pixels
    int biHeight;           // Height of images, in pixels
    short biPlanes;         // Number of planes in target device (set to 1)
    short biBitCount;       // Bits per pixel (24 in this case)
    int biCompression;      // Type of compression (0 if no compression)
    int biSizeImage;        // Image size, in bytes (0 if no compression)
    int biXPelsPerMeter;    // Resolution in pixels/meter of display device
    int biYPelsPerMeter;    // Resolution in pixels/meter of display device
    int biClrUsed;          // Number of colors in the color table (if 0, use maximum allowed by biBitCount)
    int biClrImportant;     // Number of important colors.  If 0, all colors are important
};

int write_bmp(const char *filename, int width, int height, char *rgb)
{
    int i, j, ipos;
    int bytesPerLine;
    unsigned char *line;

    FILE *file;    
    struct BMPHeader bmph;

    // The length of each line must be a multiple of 4 bytes 
    bytesPerLine = (3 * (width + 1) / 4) * 4;

    strcpy(bmph.bfType, "BM");
    bmph.bfOffBits = 54;
    bmph.bfSize = bmph.bfOffBits + bytesPerLine * height;
    bmph.bfReserved = 0;
    bmph.biSize = 40;
    bmph.biWidth = width;
    bmph.biHeight = height;
    bmph.biPlanes = 1;
    bmph.biBitCount = 24;
    bmph.biCompression = 0;
    bmph.biSizeImage = bytesPerLine * height;
    bmph.biXPelsPerMeter = 0;
    bmph.biYPelsPerMeter = 0;
    bmph.biClrUsed = 0;
    bmph.biClrImportant = 0;

    file = fopen(filename, "wb");
    if (file == NULL) return (0);

    fwrite(&bmph.bfType, 2, 1, file);
    fwrite(&bmph.bfSize, 4, 1, file);
    fwrite(&bmph.bfReserved, 4, 1, file);
    fwrite(&bmph.bfOffBits, 4, 1, file);
    fwrite(&bmph.biSize, 4, 1, file);
    fwrite(&bmph.biWidth, 4, 1, file);
    fwrite(&bmph.biHeight, 4, 1, file);
    fwrite(&bmph.biPlanes, 2, 1, file);
    fwrite(&bmph.biBitCount, 2, 1, file);
    fwrite(&bmph.biCompression, 4, 1, file);
    fwrite(&bmph.biSizeImage, 4, 1, file);
    fwrite(&bmph.biXPelsPerMeter, 4, 1, file);
    fwrite(&bmph.biYPelsPerMeter, 4, 1, file);
    fwrite(&bmph.biClrUsed, 4, 1, file);
    fwrite(&bmph.biClrImportant, 4, 1, file);

    line = malloc(bytesPerLine);
    if (line == NULL) {
        fprintf(stderr, "Can't allocate memory for BMP file.\n");
        return (0);
    }

    for (i = height - 1; i >= 0; i--) {
        for (j = 0; j < width; j++) {
            ipos = 3 * (width * i + j);
            line[3 * j] = rgb[ipos + 2];
            line[3 * j + 1] = rgb[ipos + 1];
            line[3 * j + 2] = rgb[ipos];
        }
        fwrite(line, bytesPerLine, 1, file);
    }

    free(line);
    fclose(file);

    return (1);
}

void putpx(int x, int y)
{
    // Write RGB value to specific pixel on the created bitmap
    int rgb1, rgb2, rgb3;
    switch (printColour) {
    case 0:
        // Black
        rgb1 = 0;
        rgb2 = 0;
        rgb3 = 0;
        break;
    case 1:
        // Magenta
        rgb1 = 255;
        rgb2 = 0;
        rgb3 = 255;
        break;
    case 2:
        // Cyan
        rgb1 = 0;
        rgb2 = 255;
        rgb3 = 255;
        break;        
    case 3:
        // Violet
        rgb1 = 238;
        rgb2 = 130;
        rgb3 = 238;
        break;
    case 4:
        // Yellow
        rgb1 = 255;
        rgb2 = 255;
        rgb3 = 0;
        break;
    case 5:
        // Red
        rgb1 = 255;
        rgb2 = 0;
        rgb3 = 0;
        break;
    case 6:
        // Green
        rgb1 = 0;
        rgb2 = 255;
        rgb3 = 0;
        break;
    case 7:
        // White
        rgb1 = 255;
        rgb2 = 255;
        rgb3 = 255;
        break;
    }
    int pos = y * 3 * 1984 + x * 3;    
    printermemory[pos + 0] = rgb1;
    printermemory[pos + 1] = rgb2;
    printermemory[pos + 2] = rgb3;
}

/*
 * Set the pixel at (x, y) to the given value
 * NOTE: The surface must be locked before calling this!
 */
static float divi = 1.0;  // divider for lower resolution
void putpixel(SDL_Surface * surface, int x, int y, Uint32 pixel)
{
    // if we are out of scope don't putpixel, otherwise we'll get a segmentation fault
    if (x > 1983) return;
    if (y > 2524) return;
    putpx(x, y);    // Aufruf f?Speicherplot
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
    if (pixel == 0x0000000) {
        switch (printColour) {
        case 0:
            // Black
            pixel = 0x0000000;
            break;
        case 1:
            // Magenta
            pixel = 0x0FF00FF;
            break;
        case 2:
            // Cyan
            pixel = 0x000FFFF;
            break;        
        case 3:
            // Violet
            pixel = 0x0EE82EE;
            break;
        case 4:
            // Yellow
            pixel = 0x0FFFF00;
            break;
        case 5:
            // Red
            pixel = 0x0FF0000;
            break;
        case 6:
            // Green
            pixel = 0x000FF00;
            break;
        case 7:
            // White
            pixel = 0x0FFFFFF;
            break;
        }    
    }

    switch (bpp) {
        case 1:
            *p = pixel;
            break;
        case 2:
            *(Uint16 *) p = pixel;
            break;
        case 3:
            if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                p[0] = (pixel >> 16) & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = pixel & 0xff;
            } else {
                p[0] = pixel & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = (pixel >> 16) & 0xff;
            }
            break;
        case 4:
            *(Uint32 *) p = pixel;
            break;
    }
}

int cpi = 12;
int pitch = 12; //Same like cpi but will retain its value when condensed printing is switched on
int marginleft = 0, marginright = 99;       // in characters
int marginleftp = 0, marginrightp = 1984;   // in pixels
int dpih = 240, dpiv = 216;                 // resolution in dpi
int needles = 24;                           // number of needles
int rows = 0;
double xpos = 0, ypos = 0;                  // position of print head

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
unsigned char hTabulators[35];              // List of tabulators
unsigned char vTabulators[20];              // List of vertical tabulators
int curHtab = 0;                            // next active horizontal tab
int curVtab = 0;                            // next active vertical tab
float hPixelWidth, vPixelWidth;
FILE *f;
SDL_Surface *display;
char fontx[2049000];

void erasepage()
{
    int i;  
    for (i = 0; i < 1984 * 2525 * 3; i++) printermemory[i] = 255;
}

void erasesdl()
{
    int i, t;
    if (sdlon == 0) return;
    // 1984*2525
    for (i = 0; i < 1984; i++) {
        for (t = 0; t < 2525; t++) {
            putpixel(display, i, t, 0x00FFFFFF);
        }
    }
}

int test_for_new_paper()
{
    // if we are out of paper
    if ((ypos > (2525 - 17 * vPixelWidth)) || (state == 0)) {
        xpos = marginleftp;
        ypos = 0;
        sprintf(filenameX, "%spage%d.bmp", pathbmp, page);
        printf("write   = %s \n", filenameX);
        write_bmp(filenameX, 1984, 2525, printermemory);
        
        // Create pdf file
        sprintf(filenameX, "convert  %spage%d.bmp  %spage%d.pdf ", pathbmp,
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
            page = dirX(pathbmp);
            reduce_pages(page, pathbmp);
            page = dirX(pathpdf);
            reduce_pages(page, pathpdf);
            page = dirX(pathpdf) + 1;
        }
    }
    state = 1;
}

void
_1pin_line_print(int dotColumns, float hPixelWidth, float vPixelWidth,
     float xzoom, float yzoom, int rleEncoded)
{
    // SASCHA - this needs to be checked
    int i, fByte, xByte, j;
    unsigned int xd, repeater;
    test_for_new_paper();
    if (rleEncoded) {
        for (i = 0; i < dotColumns; i++) {
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_1p;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    state = 0;
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    startzeit = tvx.tv_sec;                    
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        clock_gettime(CLOCK_REALTIME, &tvx);
                        if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_1p;
                    }
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xd & 128) putpixel(display, xpos, ypos + xByte, 0x0000000);
                        xd = xd << 1;
                    }
                    xpos = xpos + hPixelWidth;
                    i++;
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
            } else {
                // Repeat following byte (257 - repeater) times
                repeater = 257 - repeater;
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;                
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_1p;
                }
                for (j = 0; j < repeater; j++) {
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xd & 128) putpixel(display, xpos, ypos + xByte, 0x0000000);
                        xd = xd << 1;
                    }
                    xpos = xpos + hPixelWidth;
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                i++;
            }
        }        
    } else {    
        for (i = 0; i < dotColumns; i++) {
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            while (state == 0) {
                state = read_byte_from_printer((char *) &xd);  // byte1
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_1p;
            }
            for (xByte = 0; xByte < 8; xByte++) {
                if (xd & 128) putpixel(display, xpos, ypos + xByte, 0x0000000);
                xd = xd << 1;
            }
            xpos = xpos + hPixelWidth;
            // SDL_UpdateRect(display, 0, 0, 0, 0);
        }
    }
  raus_1p:
    return;
}

void
_8pin_line_print_72dpi(int dotColumns, float hPixelWidth, float vPixelWidth,
           float xzoom, float yzoom, int rleEncoded)
{
    int opr, fByte, j;
    unsigned int xd, repeater;

    test_for_new_paper();
    if (rleEncoded) {
        for (opr = 0; opr < dotColumns; opr++) {
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p_72dpi;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    state = 0;
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    startzeit = tvx.tv_sec;                    
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        clock_gettime(CLOCK_REALTIME, &tvx);
                        if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p_72dpi;
                    }                    
                    for (fByte = 0; fByte < 8; fByte++) {
                        if (xd & 128) putpixelbig(xpos, ypos + fByte * dpiv / 72, 1, 1);
                        xd = xd << 1;
                    }
                    xpos = xpos + hPixelWidth;
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                    opr++;
                }
            } else {
                // Repeat following byte (257 - repeater) times
                repeater = 257 - repeater;
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;                 
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p_72dpi;
                }
                for (j = 0; j < repeater; j++) {
                    for (fByte = 0; fByte < 8; fByte++) {
                        if (xd & 128) putpixelbig(xpos, ypos + fByte * dpiv / 72, 1, 1);
                        xd = xd << 1;
                    }
                    xpos = xpos + hPixelWidth;
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                opr++;
            }
        }        
    } else {    
        for (opr = 0; opr < dotColumns; opr++) {
            // timeout
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            while (state == 0) {
                state = read_byte_from_printer((char *) &xd);  // byte1
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p_72dpi;
            }
            if ((dotColumns - opr) == 3) opr = opr; // SASCHA - what is this intended to do?
            for (fByte = 0; fByte < 8; fByte++) {
                if (xd & 128) putpixelbig(xpos, ypos + fByte * dpiv / 72, 1, 1);
                xd = xd << 1;
            }
            xpos = xpos + hPixelWidth;
        }
    }
  raus_8p_72dpi:
    return;
}


void
_8pin_line_print(int dotColumns, float hPixelWidth, float vPixelWidth,
     float xzoom, float yzoom, int rleEncoded)
{
    int opr, fByte, j;
    unsigned int xd, repeater;
    test_for_new_paper();
    if (rleEncoded) {
        for (opr = 0; opr < dotColumns; opr++) {
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    state = 0;
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    startzeit = tvx.tv_sec;                    
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        clock_gettime(CLOCK_REALTIME, &tvx);
                        if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p;
                    }                    
                    for (fByte = 0; fByte < 8; fByte++) {
                        if (xd & 128) putpixelbig(xpos, ypos2 + fByte * vPixelWidth, hPixelWidth, vPixelWidth);
                        xd = xd << 1;
                    }
                    xpos = xpos + hPixelWidth;
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                    opr++;
                }
            } else {
                // Repeat following byte (257 - repeater) times
                repeater = 257 - repeater;
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;                 
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p;
                }
                for (j = 0; j < repeater; j++) {
                    for (fByte = 0; fByte < 8; fByte++) {
                        if (xd & 128) putpixelbig(xpos, ypos2 + fByte * vPixelWidth, hPixelWidth, vPixelWidth);
                        xd = xd << 1;
                    }
                    xpos = xpos + hPixelWidth;
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                opr++;
            }
        }        
    } else {      
        for (opr = 0; opr < dotColumns; opr++) {
            // timeout
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            while (state == 0) {
                state = read_byte_from_printer((char *) &xd);  // byte1
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_8p;
            }

            if ((dotColumns - opr) == 3) opr = opr; // SASCHA - what is this intended to do?
            for (fByte = 0; fByte < 8; fByte++) {
                if (xd & 128) putpixelbig(xpos, ypos + fByte * vPixelWidth, hPixelWidth, vPixelWidth);
                xd = xd << 1;
            }
            xpos = xpos + hPixelWidth;
        }
    }
  raus_8p:
    return;
}

void
_24pin_line_print(int dotColumns, float hPixelWidth, float vPixelWidth,
      float xzoom, float yzoom, int rleEncoded)
{
    int i, fByte, xByte, j;
    unsigned int xd, repeater;
    test_for_new_paper();
    if (rleEncoded) {
        for (i = 0; i < dotColumns; i++) {            
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            fByte = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_24p;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    ypos2 = ypos + fByte * (8 * vPixelWidth);                    
                    state = 0;
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    startzeit = tvx.tv_sec;                    
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        clock_gettime(CLOCK_REALTIME, &tvx);
                        if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_24p;
                    }                    
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xd & 128) putpixel(display, xpos, ypos2 + xByte, 0x0000000);
                        xd = xd << 1;
                    }
                    if (fByte == 3) {
                        xpos = xpos + hPixelWidth;
                    }
                    fByte++;
                    if (fByte > 3) fByte = 0;                     
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                    i++;
                }
            } else {
                // Repeat following byte (257 - repeater) times
                repeater = 257 - repeater;
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;                 
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_24p;
                }
                for (j = 0; j < repeater; j++) {
                    ypos2 = ypos + fByte * (8 * vPixelWidth);                                         
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xd & 128) putpixel(display, xpos, ypos2 + xByte, 0x0000000);
                        xd = xd << 1;
                    }
                    if (fByte == 3) {
                        xpos = xpos + hPixelWidth;
                    }
                    fByte++;
                    if (fByte > 3) fByte = 0;                     
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                i++;
            }
        }         
    } else {      
        for (i = 0; i < dotColumns; i++) {
            for (fByte = 0; fByte < 4; fByte++) {
                ypos2 = ypos + fByte * (8 * vPixelWidth);
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;

                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte1
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_24p;
                }
                for (xByte = 0; xByte < 8; xByte++) {
                    if (xd & 128) putpixel(display, xpos, ypos2 + xByte, 0x0000000);
                    xd = xd << 1;
                }
            }
            xpos = xpos + hPixelWidth;
            // SDL_UpdateRect(display, 0, 0, 0, 0);
        }
    }
  raus_24p:
    return;
}

void
_48pin_line_print(int dotColumns, float hPixelWidth, float vPixelWidth,
      float xzoom, float yzoom, int rleEncoded)
{
    // SASCHA - this needs to be checked - 6 bytes per column
    int i, fByte, xByte, j;
    unsigned int xd, repeater;
    test_for_new_paper();
    if (rleEncoded) {
        for (i = 0; i < dotColumns; i++) {            
            state = 0;
            clock_gettime(CLOCK_REALTIME, &tvx);
            startzeit = tvx.tv_sec;
            fByte = 0;
            while (state == 0) {
                state = read_byte_from_printer((char *) &repeater);  // number of times to repeat next byte
                clock_gettime(CLOCK_REALTIME, &tvx);
                if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_48p;
            }
            if (repeater <= 127) {
                repeater++;
                // string of data byes to be printed
                for (j = 0; j < repeater; j++) {
                    ypos2 = ypos + fByte * (4 * vPixelWidth);                    
                    state = 0;
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    startzeit = tvx.tv_sec;                    
                    while (state == 0) {
                        state = read_byte_from_printer((char *) &xd);  // byte to be printed
                        clock_gettime(CLOCK_REALTIME, &tvx);
                        if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_48p;
                    }                    
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xd & 128) putpixel(display, xpos, ypos2 + xByte, 0x0000000);
                        xd = xd << 1;
                    }
                    if (fByte == 6) {
                        xpos = xpos + hPixelWidth;
                    }
                    fByte++;
                    if (fByte > 6) fByte = 0;                     
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                    i++;
                }
            } else {
                // Repeat following byte (257 - repeater) times
                repeater = 257 - repeater;
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;                 
                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte to be printed
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_48p;
                }
                for (j = 0; j < repeater; j++) {
                    ypos2 = ypos + fByte * (4 * vPixelWidth);                                         
                    for (xByte = 0; xByte < 8; xByte++) {
                        if (xd & 128) putpixel(display, xpos, ypos2 + xByte, 0x0000000);
                        xd = xd << 1;
                    }
                    if (fByte == 6) {
                        xpos = xpos + hPixelWidth;
                    }
                    fByte++;
                    if (fByte > 6) fByte = 0;                     
                    // SDL_UpdateRect(display, 0, 0, 0, 0);
                }
                i++;
            }
        }       
    } else {      
        for (i = 0; i < dotColumns; i++) {
            for (fByte = 0; fByte < 7; fByte++) {
                ypos2 = ypos + fByte * (4 * vPixelWidth); // Reduced to 4 x vPixelWidth to allow for 48 pins
                state = 0;
                clock_gettime(CLOCK_REALTIME, &tvx);
                startzeit = tvx.tv_sec;

                while (state == 0) {
                    state = read_byte_from_printer((char *) &xd);  // byte1
                    clock_gettime(CLOCK_REALTIME, &tvx);
                    if (((tvx.tv_sec - startzeit) >= timeout) && (state == 0)) goto raus_48p;
                }
                for (xByte = 0; xByte < 8; xByte++) {
                    if (xd & 128) putpixel(display, xpos, ypos2 + xByte, 0x0000000);
                    xd = xd << 1;
                }
            }
            xpos = xpos + hPixelWidth;
            // SDL_UpdateRect(display, 0, 0, 0, 0);
        }
    }
  raus_48p:
    return;
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

void putpixelbig(int xpos, int ypos, float hwidth, float vdith)
{
    int a, b;
    for (a = 0; a < hwidth; a++) {
        for (b = 0; b < vdith; b++) {
            putpixel(display, xpos + a, ypos + b, 0x00000000);
        }
    }
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

    chr2 = (unsigned int) chr;
    adressOfChar = chr2 << 4;  // Multiply with 16 to Get Adress
    hPixelWidth = ((float) dpih / (float) cpi) / 8;
    vPixelWidth = ((float) dpiv / (float) cpi * 2) / 18;  
    // eigentlich sollte 
    // das 16 sein da
    // jedes zeichen 16
    // Pixel gro?ist,
    // 18 sieht aber
    // besser aus
    boldoffset11=0;
    boldoffset=0;
    if (bold == 1) {
        boldoffset = (hPixelWidth + vPixelWidth) / 3;
        boldoffset11=boldoffset;
        //printf("%d\n", boldoffset);
    } 
    if (italic==1) {
        italiccount=1;
    }
    test_for_new_paper();
    if (superscript==1) { 
        divisor=2.0/3.0;
        vPixelWidth=vPixelWidth*divisor;
        yposoffset=2;
    } else if (subscript==1) { 
        divisor=2.0/3.0;
        vPixelWidth=vPixelWidth*divisor;
        yposoffset=8;
    }

    if (direction_of_char == 1) {
        for (i = 0; i <= 15; i++) {
            xd = fontx[adressOfChar + i];
            if ((chr!=32) || (single_continuous_line==1) || (double_continuous_line==1)) {
                if ((i==14) && (underlined==1)    && ((single_continuous_line==1) ||  (single_broken_line))  ) xd=255;
                if ((i==13) && (underlined==1)    && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
                if ((i==15) && (underlined==1)    && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;

                if ((i==8 ) && (strikethrough==1) && ((single_continuous_line==1) ||  (single_broken_line))  ) xd=255;
                if ((i==7 ) && (strikethrough==1) && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
                if ((i==9 ) && (strikethrough==1) && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;

                if ((i==1 ) && (overscore==1)     && ((single_continuous_line==1) ||  (single_broken_line))  ) xd=255;
                if ((i==0 ) && (overscore==1)     && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
                if ((i==3 ) && (overscore==1)     && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
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
            if ((chr!=32) || (single_continuous_line==1) || (double_continuous_line==1)) {
                if ((i==14) && (underlined==1)    && ((single_continuous_line==1) ||  (single_broken_line))  ) xd=255;
                if ((i==13) && (underlined==1)    && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
                if ((i==15) && (underlined==1)    && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
      
                if ((i==8 ) && (strikethrough==1) && ((single_continuous_line==1) ||  (single_broken_line))  ) xd=255;
                if ((i==7 ) && (strikethrough==1) && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
                if ((i==9 ) && (strikethrough==1) && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;

                if ((i==1 ) && (overscore==1)     && ((single_continuous_line==1) ||  (single_broken_line))  ) xd=255;
                if ((i==0 ) && (overscore==1)     && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
                if ((i==3 ) && (overscore==1)     && ((double_continuous_line==1) ||  (double_broken_line))  ) xd=255;
            }
            for (fByte = 0; fByte < 8; fByte++) {
                if (xd & 001) putpixelbig(xpos + fByte * hPixelWidth+italiccount*(7-i),ypos + yposoffset+ i * vPixelWidth,
                                hPixelWidth + boldoffset, vPixelWidth + boldoffset11);
                xd = xd >> 1;
            }
        }
    }
    xpos = xpos + hPixelWidth * 8;
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
    if (xpos > (1983 - vPixelWidth * 16)) {
        xpos = marginleftp;
        ypos = ypos + vPixelWidth * 16;
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
    strcat(pathcreator, "bmp  2>>/dev/null");
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
    sprintf(x,"mkdir %s 2>>/dev/null",pathbmp);
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
    // DIN A4 has a width 210 mm and height of 297 mm .
    // the needles have a distance of 72dpi 
    // 240dpi resolution from left to right
    // 216dpi top down
    // so this defines an DIN A4 Paper having 1984x2525pixels (width x height)

    unsigned int xd = 0;
    unsigned int pixelz = 0;
    int xposold;
    int i, countcharz = 0;
    char *config;
    FILE *FL; 

    cpulimit();
    if (argc < 5) {
        printf
            ("Usage: ./PrinterConvert <timeout> <divisor> <font> <font_direction> <sdl> <path> \n\n");

        printf("Usage: ./PrinterConvert 4 3 font.raw 1 sdlon /home/pi/data\n \n");
        printf
            ("timeout=4          --> if no char comes in within 4 sec, printout current page\n");
        printf
            ("divisor=30         --> reduce sdl display to 30% of original size\n");
        printf("font=font.raw      --> rload this font in font memory area\n");
        printf("font_direction=1   --> 0=lsb left 1=lsb right\n");
        printf("sdl=sdlon          --> display printout in sdl window\n");
        printf("path=/home/pi/data --> store all in this directory\n");
        printf("offlineswitch      --> evaluate offline switch\n");

        goto raus;
    }
    printf("\n");

    // Parse the parameters in case they override the defaults

    for (i = 0; i < argc; i++) {
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
        
        if (strncmp(param, "offlineswitch", strlen(args[i])) == 0) offlineswitch = 1;

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
    
    printf("delays arround ack: t1=%d    t2=%d    t3=%d    t4=%d    t5=%d\n",t1,t2,t3,t4,t5);
    
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
        strcpy(pathbmp, path);
        strcpy(pathpcl, path);
        strcpy(patheps, path);
    }

    strcpy(pathpdf,    path);
    strcat(pathraw,   "raw/");
    strcat(pathbmp,   "bmp/");
    strcat(pathpdf,   "pdf/");
    strcat(pathpcl,   "pcl/");
    strcat(patheps,   "eps/");

    makeallpaths();

    page = dirX(pathraw);
    reduce_pages(page, pathraw);
    page = dirX(pathbmp);
    reduce_pages(page, pathbmp);

    page = dirX(pathpdf);
    reduce_pages(page, pathpdf);
    page = dirX(pathpdf) + 1;

    // goto raus;
    initialize();
    test_offline_switch(offlineswitch);

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
    xdim = 1984 / divi;
    ydim = 2525 / divi;
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
    test_offline_switch(offlineswitch);
    if (sdlon) erasesdl();
    erasepage();
    // Clear tab marks
    for (i = 0; i < 32; i++) hTabulators[i] = 0;
    for (i = 0; i < 16; i++) vTabulators[i] = 0;

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
              
                ypos = ypos + (float) 30*((float)pitch/(float)cpi);  // Those are 8 Pixels forward concerning the
                // printheads 72dpi resolution. 
                // Cause the mechanics has a resolution of 216 dpi (which is 3 times
                // higher) 8x3=24 steps/pixels has to be done
                xpos = marginleftp;
                double_width = 0;
                break;
            case 12:    // form feed (neues blatt) 
                ypos = 2525;  // just put it in an out of area position
                test_for_new_paper();
                i = 0;
                double_width = 0;
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
                    cpi                    =  12;
                    pitch                  =  12;
                    dpih                   = 240; 
                    dpiv                   = 216;
                    printColour            =   0;
                    bold                   =   0;
                    italic                 =   0;
                    underlined             =   0;
                    superscript            =   0;
                    subscript              =   0;
                    strikethrough          =   0;
                    overscore              =   0;
                    single_continuous_line =   0;
                    double_continuous_line =   0;
                    single_broken_line     =   0;
                    double_broken_line     =   0;
                    double_width           =   0;
                    outline_printing       =   0;
                    shadow_printing        =   0;
                    print_controlcodes     =   0;
                    print_uppercontrolcodes =  0;
                    graphics_mode          =   0;
                    microweave_printing    =   0;
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
                                vPixelWidth = (float) dpiv / (float) 720;
                                break;
                            case 10:
                                vPixelWidth = (float) dpiv / (float) 360;
                                break;                                
                            case 20:
                                vPixelWidth = (float) dpiv / (float) 180;
                                break;                                
                        }
                        switch (h) {
                            case 5:
                                hPixelWidth = (float) dpih / (float) 720;
                                break;
                            case 10:
                                hPixelWidth = (float) dpih / (float) 360;
                                break;                                
                            case 20:
                                hPixelWidth = (float) dpih / (float) 180;
                                break;                                
                        }
                        dotColumns = nH << 8;
                        dotColumns = dotColumns | nL;                        
                        if ( microweave_printing == 1 ) m = 1; 
                        switch (c) {
                        case 0:
                            // Normal graphics - non-compressed
                            switch (m) {
                                case 1:
                                    _1pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0);
                                    break;
                                case 8:
                                    _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0);
                                    break;
                                case 24:
                                    _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0);
                                    break;
                            }
                            break;
                        case 1:
                            // Normal graphics - RLE mode
                            switch (m) {
                                case 1:
                                    _1pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
                                    break;
                                case 8:
                                    _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
                                    break;
                                case 24:
                                    _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 1);
                                    break;
                            }
                            break;                            
                            break;
                        case 2:
                            // TIFF compressed mode
                            break;
                        case 3:
                            // Delta Row compressed mode
                            break;
                        }
                        break;

                case '+':    // Set n/360-inch line spacing ESC + n
                    state = read_byte_from_printer((char *) &xd);
                    ypos = ypos + (int) xd *360 / 180; // Is this correct SASCHA?
                    break;
                case '(':    
                    state = read_byte_from_printer((char *) &nL);
                    switch (nL) {
                    case 'C': 
                        // 27 40 67 nL nH mL mH Set page length in defined unit
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        break;
                    case 'c': 
                        // 27 40 67 nL nH tL tH bL bH Set page format
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bH); 
                        break;
                    case 'V': 
                        // 27 40 86 nL nH mL mH Set absolute vertical print position
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        break;
                    case 'v': 
                        // 27 40 118 nL nH mL mH Set relative vertical print position
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        break;
                    case 'U': 
                        // 27 40 85 nL nH m Set unit
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m); 
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
                case '$':    // Set absolute horizontal print position 0 = nH
                    // = 127 0 = nL = 255 (horizontal position) =
                    // ((nH ?256) + nL) ?(1/60 inch) + (left margin) 
                    // ESC $ nL nH
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nH);
                    xpos = ((nH * 256) + nL) * (dpih / 60) + marginleftp;
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
                    hPixelWidth = ((float) dpih / (float) cpi) / 8;
                    vPixelWidth = ((float) dpiv / (float) cpi * 2) / 18;
                    xpos = xpos - hPixelWidth * 8;
                    if (xpos < 0) xpos = xposold;
                }
                break;
            case 9:    // TAB
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    if (hTabulators[curHtab] == 0) curHtab = 0; // No more tab marks
                    if (hTabulators[0] > 0) {
                        // forward numbercount of tabulator[curHtab ]characters to the right 
                        xpos = (dpih * hTabulators[curHtab]) / cpi;
                        curHtab++;
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
                  
                    ypos = ypos + (float) 30*((float)pitch/(float)cpi);  // Those are 8 Pixels forward concerning the
                    // printheads 72dpi resolution. 
                    // Cause the mechanics has a resolution of 216 dpi (which is 3 times
                    // higher) 8x3=24 steps/pixels has to be done
                    xpos = marginleftp;
                    double_width = 0;
                }
                break;
            case 11:    // VT vertical tab (same like 10)
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    //changed from 24 to 30 20.09.2015
                    // To be re-written to take account of vertical tab stops - TO DO RWAP
                    ypos = ypos + 30;
                    xpos = marginleftp;
                    // if all tabs cancelled with ESC B NUL, then this acts as CR only but does not cancel double width
                    double_width = 0; // ONLY if all tabs have not been cancelled
                }
                break;
            case 12:    // form feed (neues blatt) 
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    ypos = 2525;  // just put it in an out of area position
                    test_for_new_paper();
                    i = 0;
                    double_width = 0;
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
                    double_width = 1;
                }
                break;
            case 15:    // SI Shift In (do nothing) Condensed printing on
                if (print_controlcodes) {
                    print_character(xd);
                } else {                
                    if (pitch==10) cpi=17.14;
                    if (pitch==12) cpi=20;
                    // Add for proportional font = 1/2 width
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
                    double_width = 0;
                }
                break;
            case 21:    // NAK Negative Acknowledgement (do nothing)
                break;
            case 22:    // Syn Synchronus idle (do nothing)
                break;
            case 23:    // ETB End Of Transmition Block (do nothing)
                break;
            case 24:    // CAN Cancel (do nothing)
                if (print_controlcodes) {
                    print_character(xd);
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
                break;
            case 255:
                xposold = xpos;
                hPixelWidth = ((float) dpih / (float) cpi) / 8;
                vPixelWidth = ((float) dpiv / (float) cpi * 2) / 18;
                xpos = xpos + hPixelWidth * 8;
                if (xpos > (1983 - vPixelWidth * 16)) {
                    xpos = marginleftp;
                    ypos = ypos + vPixelWidth * 16;
                }      // 24=8*216/72
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
                    cpi                    =  12;
                    pitch                  =  12;
                    dpih                   = 240; 
                    dpiv                   = 216;
                    printColour            =   0;
                    bold                   =   0;
                    italic                 =   0;
                    underlined             =   0;
                    superscript            =   0;
                    subscript              =   0;
                    strikethrough          =   0;
                    overscore              =   0;
                    single_continuous_line =   0;
                    double_continuous_line =   0;
                    single_broken_line     =   0;
                    double_broken_line     =   0;
                    double_width           =   0;
                    outline_printing       =   0;
                    shadow_printing        =   0;
                    print_controlcodes     =   0;
                    print_uppercontrolcodes =  0;
                    break;                        
                case '0':    //Select 1/8 inch line spacing 
                    ypos = ypos + (int) 1 *8 / 180; // Is this correct SASCHA?
                    break;
                case '1':    //Select 7/72 inch line spacing 
                    ypos = ypos + (int) 7 *72 / 180; // Is this correct SASCHA?
                    break;
                case '2':    // Select 1/6-inch line spacing
                    ypos = ypos + (int) 1 *6 / 180; // Is this correct SASCHA?
                    break;
                case '3':    // Set n/216-inch line spacing ESC 3 n
                    state = read_byte_from_printer((char *) &xd);
                    ypos = ypos + (int) xd *216 / 180;
                    // SASCHA - surely this only affects the current line - what about all lines below?
                    break;                        
                case '+':    // Set n/360-inch line spacing ESC + n
                    state = read_byte_from_printer((char *) &xd);
                    ypos = ypos + (int) xd *360 / 180; // Is this correct SASCHA?
                    break;
                case 'A':    // ESC A n Set n/60 inches or
                    // n/72 inches line spacing (24 or 9 pin printer)
                    state = read_byte_from_printer((char *) &xd);
                    if (needles == 9) {
                        // go xp/60
                        ypos = ypos + (int) xd *60 / 180; // Is this correct SASCHA?
                    } else {  // needles must be 24 here
                        // go xp/72
                        ypos = ypos + (int) xd *72 / 180; // Is this correct SASCHA?
                    }
                    break;
                case 'M':    // ESC M Select 10.5-point, 12-cpi
                    cpi = 12;
                    pitch=12;
                    break;                        
                case 'P':    // ESC P Set 10 cpi
                    cpi = 10;
                    pitch=10;
                    break;
                case 'g':    // ESC g Select 10.5-point, 15-cpi
                    cpi = 15;
                    pitch=15;
                    break;
                case 20:    // ESC SP Set intercharacter space
                    // Not yet implemented
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'l':    // ESC l m set the left margin m in characters
                    state = read_byte_from_printer((char *) &xd);
                    marginleft = (int) xd;
                    marginleftp = (dpih / cpi) * marginleft;  // rand in pixels
                    // von links
                    // Wenn Marginleft ausserhalb des bereiches dann auf 0 setzen
                    // (fehlerbehandlung)
                    if (marginleftp > 1984) marginleftp = 0;
                    break;
                case 'D':    // ESC D n1,n2,n3 .... nk NUL, horizontal tab
                    // positions n1..nk
                    i = 0;
                    while ((xd != 0) && (i < 32)) {
                        // maximum 32 tabs are allowed last
                        // tab is always 0 to finish list
                        state = read_byte_from_printer((char *) &xd);
                        hTabulators[i] = xd;
                        i++;
                    }
                    break;
                case 'B':    // ESC B n1,n2,n3 .... nk NUL, vertical tab
                    // positions n1..nk
                    i = 0;
                    while ((xd != 0) && (i < 16)) {
                        // maximum 16 tabs are allowed last
                        // tab is always 0 to finish list
                        state = read_byte_from_printer((char *) &xd);
                        vTabulators[i] = xd;
                        i++;
                    }
                    break;                        
                case 'b':    // ESC b n1,n2,n3 .... nk NUL, vertical tab input VFU channels
                    // positions n1..nk
                    i = 0;
                    while ((xd != 0) && (i < 8)) {
                        // maximum 8 tabs are allowed last
                        // tab is always 0 to finish list
                        state = read_byte_from_printer((char *) &xd);
                        // vTabulators[i] = xd;
                        i++;
                    }
                    break;
                case '/':    // ESC / m, set vertical tab channel
                    // positions n1..nk
                    // Not implemented
                    state = read_byte_from_printer((char *) &m);
                    break;                         
                case 'e':    // ESC e m n, set fixed tab increment
                    // positions n1..nk
                    // Not implemented yet
                    state = read_byte_from_printer((char *) &m);
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'a':    // ESC a n, set justification for text
                    // Not implemented
                    state = read_byte_from_printer((char *) &nL);
                    break;                         
                case 'L':    // ESC l m set the left margin m in characters
                    needles = 9;
                    hPixelWidth = (float) dpih / (float) 120;
                    vPixelWidth = (float) dpiv / (float) 72;  // Das sind hier 3
                    // Pixels
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    // nL = fgetc(f);
                    // nH = fgetc(f);
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0);
                    break;
                case 'Q':    // ESC Q m set the right margin
                    state = read_byte_from_printer((char *) &xd);
                    marginright = (int) xd;
                    marginrightp = (dpih / cpi) * marginright;  // rand in pixels
                    // von links
                    break;
                case 'J':    // ESC J m Forward paper feed m/180 inches or
                    // m/216 inches (24 or 9 pin printer)
                    state = read_byte_from_printer((char *) &xd);
                    if (needles == 9) {
                        // go xp/216
                        ypos = ypos + (int) xd *dpiv / 216;  // here ste
                        // printf("forward %d/180 \n",xd);
                    } else {  // needles must be 24 here
                        // go xp/180
                        ypos = ypos + (int) xd *dpiv / 180;
                    }
                    break;
                case 'K':    // ESC K n1 n2 data 8-dot single-density bit image 
                    needles = 9;
                    hPixelWidth = (float) dpih / (float) 40;
                    vPixelWidth = (float) dpiv / (float) 72;  // Das sind hier 3
                    // Pixels
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    // nL = fgetc(f);
                    // nH = fgetc(f);
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0);
                    // if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
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
                case 'Z':    // ESC K n1 n2 data 8-dot single-density bit image
                    // (atari ste)
                    needles = 9;
                    hPixelWidth = (float) dpih / (float) 240;
                    vPixelWidth = (float) dpiv / (float) (216 / 1);  // Das sind hier 3
                    // Pixels
                    state = read_byte_from_printer((char *) &nL);
                    if (state == 0) break;
                    state = read_byte_from_printer((char *) &nH);
                    if (state == 0) break;
                    dotColumns = nH << 8;
                    dotColumns = dotColumns | nL;
                    _8pin_line_print_72dpi(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0);
                    break;
                case 'x':    // Select LQ or draft ESC x n n can be 0 or 48 for
                    // draft and 1 or 49 for letter quality
                    // not implemented yet
                    // maybe not really needed
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'p':    // ESC p n Turn proportional mode on/off off--> n=0
                    // or 48 on --> n=1 or 49
                    // not implemented yet
                    // maybe not really needed
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'E':    // ESC E SELECT BOLD FONT
                    bold = 1;
                    break;
                case 'F':    // ESC F CANCEL BOLD FONT
                    bold = 0;
                    break;
                case 'G':    // ESC H SELECT DOUBLE STRIKE
                    // For now this is same as setting bold
                    bold = 1;
                    break;
                case 'H':    // ESC G CANCEL DOUBLE STRIKE
                    // For now this is same as cancelling bold
                    bold = 0;
                    break;                        
                case '-':    // ESC - SELECT UNDERLINED FONT
                    state = read_byte_from_printer((char *) &nL);
                    if ((nL==1) || (nL==49)) underlined=1;
                    if ((nL==0) || (nL==48)) underlined=0; 
                    break;
                case 'W':    // ESC W SELECT DOUBLE WIDTH
                    // Not yet implemented
                    state = read_byte_from_printer((char *) &nL);
                    if ((nL==1) || (nL==49)) double_width=1;
                    if ((nL==0) || (nL==48)) double_width=0; 
                    break;                        
                case 'w':    // ESC w SELECT DOUBLE HEIGHT
                    // Not yet implemented
                    state = read_byte_from_printer((char *) &nL);
                    if ((nL==1) || (nL==49)) double_height=1;
                    if ((nL==0) || (nL==48)) double_height=0; 
                    break;                         
                case '4':    // ESC 4 SELECT ITALIC FONT
                    italic = 1;
                    break;    
                case '5':    // ESC 5 CANCEL ITALIC FONT
                    italic = 0;
                    break;
                case '!':    // ESC ! n Master Font Select
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
                        // Not yet implemented;
                        double_width = 1;
                    } else {
                        // Cancel Double Width
                        // Not yet implemented
                        double_width = 0;
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
                            underlined                 = 0;
                            strikethrough              = 0;
                            overscore                  = 0;
                            single_continuous_line     = 0;
                            double_continuous_line     = 0;
                            single_broken_line         = 0;
                            double_broken_line         = 0;

                            switch (d1) {
                            case 1: 
                                underlined             = 1;
                                break;
                            case 2:
                                strikethrough          = 1;
                                break;
                            case 3:
                                overscore              = 1;
                                break;
                            }
                            
                            switch (d2) {
                            case 0:
                                //erase all   
                                underlined             = 0;
                                strikethrough          = 0;
                                overscore              = 0;
                                single_continuous_line = 0;
                                double_continuous_line = 0;
                                single_broken_line     = 0;
                                double_broken_line     = 0;
                                break;
                            case 1:
                                single_continuous_line = 1;
                                break;
                            case 2:
                                double_continuous_line = 1;
                                break;
                            case 5:
                                single_broken_line     = 1;
                                break;
                            case 6:
                                double_broken_line     = 1;
                                break;
                            }

                            printf("underlined  %d\n",underlined);
                            printf("strikethrough  %d\n",strikethrough);
                            printf("overscore  %d\n",overscore);
                        }
                        break;
                    case 'C': 
                        // 27 40 67 nL nH mL mH Set page length in defined unit
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        break;
                    case 'c': 
                        // 27 40 67 nL nH tL tH bL bH Set page format
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &tH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &bH); 
                        break;
                    case 'V': 
                        // 27 40 86 nL nH mL mH Set absolute vertical print position
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        break;
                    case 'v': 
                        // 27 40 118 nL nH mL mH Set relative vertical print position
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &mH); 
                        break;
                    case 'U': 
                        // 27 40 85 nL nH m Set unit
                        // not implemented yet
                        state = read_byte_from_printer((char *) &nL); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &nH); 
                        if (state == 0) break;
                        state = read_byte_from_printer((char *) &m); 
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
                            // clear all user defined graphics
                            // TO BE WRITTEN
                            // Clear tab marks
                            for (i = 0; i < 32; i++) hTabulators[i] = 0;
                            for (i = 0; i < 16; i++) vTabulators[i] = 0;
                            microweave_printing = 0;
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
                    // not implemented yet
                    state = read_byte_from_printer((char *) &m);
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'c':
                    // ESC c nL nH Set Horizonal Motion Index (HMI)
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nH);
                    break;                        
                case 'X':
                    // ESC X m nL nH Select font by pitch & point
                    // not implemented yet
                    state = read_byte_from_printer((char *) &m);
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nH);                        
                    break;                        
                case 'U':    // Turn unidirectional mode on/off ESC U n n = 0 or
                    // 48 Bidirectional 1 or 49 unidirectional
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case '<':    // Unidirectional mode (1 line)
                    // not implemented yet
                    break;                          
                case '8':    // Disable paper-out detector
                    // not required
                    break;                         
                case '9':    // Enable paper-out detector
                    // not required
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
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    break;
                case 'O':    // Cancel bottom margin Cancels the top and bottom
                    // margin settings
                    // not implemented yet
                    break;
                case '$':    // Set absolute horizontal print position 0 = nH
                    // = 127 0 = nL = 255 (horizontal position) =
                    // ((nH ?256) + nL) ?(1/60 inch) + (left margin) 
                    // ESC $ nL nH
                    // not implemented yet
                    state = read_byte_from_printer((char *) &nL);
                    state = read_byte_from_printer((char *) &nH);
                    xpos = ((nH * 256) + nL) * (dpih / 60) + marginleftp;
                    break;
                case '6':    // ESC 6 Enable printing of upper control codes
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
                        switch (m) {
                        case 0:  // 60 x 60 dpi 9 needles
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 60;
                            vPixelWidth = (float) dpiv / (float) 60;  // Das sind hier 3
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 1:  // 120 x 60 dpi 9 needles
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 120;
                            vPixelWidth = (float) dpiv / (float) 60;    // Using ESC/P2 version (ESC/P was 72)
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 2:
                            // 120 x 60 dpi 9 needles - not adjacent dot printing.... (not sure what that means)
                            // Treat as per case 1 for now
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 120;
                            vPixelWidth = (float) dpiv / (float) 60;    // Using ESC/P2 version (ESC/P was 72)
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;                            
                            break;
                        case 3:
                            // 240 x 60 dpi 9 needles - not adjacent dot printing.... (not sure what that means)
                            // Treat as per case 1 for now                        
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 240;
                            vPixelWidth = (float) dpiv / (float) 60;    // Using ESC/P2 version (ESC/P was 72)
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 4:  // 80 x 60 dpi 9 needles
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 80;
                            vPixelWidth = (float) dpiv / (float) 60;    // Using ESC/P2 version (ESC/P was 72)
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 5:  // Not available in ESC/P
                            break;
                        case 6:  // 90 x 60 dpi 9 needles
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 90;
                            vPixelWidth = (float) dpiv / (float) 60;    // Using ESC/P2 version (ESC/P was 72)
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 7:  // 144 x 72 dpi 9 needles (ESC/P only)
                            needles = 9;
                            hPixelWidth = (float) dpih / (float) 144;
                            vPixelWidth = (float) dpiv / (float) 72;    // Using ESC/P2 version (ESC/P was 72)
                            // Pixels
                            _8pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 32:  // 60 x 180 dpi, 24 dots per column - row = 3 bytes
                            needles = 24;
                            hPixelWidth = (float) dpih / (float) 60;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 33:  // 120 x 180 dpi, 24 dots per column - row = 3 bytes
                            needles = 24;
                            hPixelWidth = (float) dpih / (float) 120;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 35:  // Resolution not verified possibly 240x216 sein
                            needles = 24;
                            hPixelWidth = (float) dpih / (float) 240;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 38:  // 90 x 180 dpi, 24 dots per column - row = 3 bytes
                            needles = 24;
                            hPixelWidth = (float) dpih / (float) 90;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 39:  // 180 x 180 dpi, 24 dots per column - row = 3 bytes
                            needles = 24;
                            hPixelWidth = (float) dpih / (float) 180;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 40:  // 360 x 180 dpi, 24 dots per column - row = 3 bytes
                            needles = 24;
                            hPixelWidth = (float) dpih / (float) 360;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _24pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 64:  // 60 x 60 dpi, 48 dots per column - row = 6 bytes
                            needles = 48;
                            hPixelWidth = (float) dpih / (float) 60;
                            vPixelWidth = (float) dpiv / (float) 60;  // Das sind hier 1.2 
                            // Pixels
                            _48pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 65:  // 120 x 120 dpi, 48 dots per column - row = 6 bytes
                            needles = 48;
                            hPixelWidth = (float) dpih / (float) 120;
                            vPixelWidth = (float) dpiv / (float) 120;  // Das sind hier 1.2 
                            // Pixels
                            _48pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 70:  // 90 x 180 dpi, 48 dots per column - row = 6 bytes
                            needles = 48;
                            hPixelWidth = (float) dpih / (float) 90;
                            vPixelWidth = (float) dpiv / (float) 180;  // Das sind hier 1.2 
                            // Pixels
                            _48pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 71:  // 180 x 360 dpi, 48 dots per column - row = 6 bytes
                            needles = 48;
                            hPixelWidth = (float) dpih / (float) 180;
                            vPixelWidth = (float) dpiv / (float) 360;  // Das sind hier 1.2 
                            // Pixels
                            _48pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 72:  // 360 x 360 dpi, 48 dots per column - row = 6 bytes - no adjacent dot printing
                            needles = 48;
                            hPixelWidth = (float) dpih / (float) 360;
                            vPixelWidth = (float) dpiv / (float) 360;  // Das sind hier 1.2 
                            // Pixels
                            _48pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        case 73:  // 360 x 360 dpi, 48 dots per column - row = 6 bytes
                            needles = 48;
                            hPixelWidth = (float) dpih / (float) 360;
                            vPixelWidth = (float) dpiv / (float) 360;  // Das sind hier 1.2 
                            // Pixels
                            _48pin_line_print(dotColumns, hPixelWidth, vPixelWidth, 1.0, 1.0, 0);
                            break;
                        }
                }      // end of switch
            case 15:    // ESC SO Shift Out Select double Width printing (for one line)
                double_width = 1;
                break;                    
            case 15:    // ESC SI Shift In Condensed printing on
                if (pitch==10) cpi=17.14;
                if (pitch==12) cpi=20;
                // Add for proportional font = 1/2 width
                break;
            }   // End of ESC branch
        }
        if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
    }   

    if (sdlon) SDL_UpdateRect(display, 0, 0, 0, 0);
    if (i == 0) goto main_loop_for_printing;
   
    printf("\n\nI am at page %d\n", page);

    sprintf(filenameX, "%spage%d.bmp", pathbmp, page);
    printf("write   = %s \n", filenameX);
    write_bmp(filenameX, 1984, 2525, printermemory);
    
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

    sprintf(filenameX, "convert  %spage%d.bmp  %spage%d.pdf ", pathbmp, page,pathpdf, page);
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
        page = dirX(pathbmp);
        reduce_pages(page, pathbmp);
        page = dirX(pathpdf);
        reduce_pages(page, pathpdf);
        page = dirX(pathpdf) + 1;
    }
    // sleep(1);
    goto main_loop_for_printing;

  raus:
    return 0;
}
