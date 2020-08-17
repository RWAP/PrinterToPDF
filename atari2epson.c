#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define FONT_EXT  ".C16"


static void convert(FILE* fi, FILE* fo);
static void usage(const char* progname);


int main(int argc, char* argv[])
{
	if (argc < 2) {
		usage(argv[0]);
		exit(1);
	}

	for (int i = 1; i < argc; ++i) {
		char*       infile  = argv[i];
		char*       outfile = malloc(strlen(infile) + 4);
		char*       ext     = strrchr(basename(infile), '.');

		if (ext != NULL) {
			strcpy(outfile, infile);
			strcpy(strrchr(outfile, '.'), FONT_EXT);
		} else {
			strcat(strcpy(outfile, infile), FONT_EXT);
		}

		printf("%s", infile);

		FILE* fi = fopen(infile, "rb");

		if (fi == NULL) {
			puts(" cannot be opened.");
		} else {
			FILE* fo = fopen(outfile, "wb");

			if (fo == NULL) {
				puts(": Cannot open output file.");
			} else {
				fseek(fi, 0, SEEK_END);

				if (ftell(fi) != 0x1000) {
					puts(" is not 4096 bytes long.");
				} else {
					printf(" -> %s\n", outfile);
					fseek(fi, 0, SEEK_SET);
					convert(fi, fo);
				}

				fclose(fo);
			}

			fclose(fi);
		}
	}

	return 0;
}


void
convert(FILE* fi, FILE* fo)
{
	char inbuf[0x1000];
	char outbuf[0x1000];

	fread(inbuf, 0x1000, 1, fi);

	for (int c = 0; c < 256; ++c) {
		for (int l = 0; l < 16; ++l) {
			outbuf[c * 16 + l] = inbuf[c + 256 * l];
		}
	}

	fwrite(outbuf, 0x1000, 1, fo);
}


void
usage(const char* progname)
{
	printf("Usage: %s fontfile [fontfile ...]\n", progname);
}

