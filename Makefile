prefix = /usr

CC = gcc
RM = rm -f

INSTALL = install -c
INSTALL_PRG = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

ifneq ($(shell uname -p),x86_64)
CFLAGS = -O3 \
         -mfloat-abi=hard \
         -funsafe-math-optimizations \
         -mno-unaligned-access \
         $(shell sdl-config --cflags)
else
CFLAGS = -O3 \
         -funsafe-math-optimizations \
         $(shell sdl-config --cflags)
endif

LDFLAGS = $(shell sdl-config --libs)
LDLIBS = -lrt -lhpdf -lpng

PRG = printerToPDF
SRC = PrinterConvert.c
OBJ = $(SRC:%.c=%.o)
FNT = $(wildcard font2/*.C16)

PRGPATH = $(prefix)/bin
DATAPATH = $(prefix)/lib/PrinterToPDF
FONTPATH = $(DATAPATH)/font2


.PHONY: all install clean


all:	$(PRG)

clean:
	$(RM) $(PRG) $(OBJ)

install:	all
	$(INSTALL_PRG) $(PRG) "$(PRGPATH)"
	$(INSTALL) -d -D "$(FONTPATH)"
	$(INSTALL_DATA) $(FNT) "$(FONTPATH)"

$(PRG):	$(OBJ)
	$(CC) -o $(PRG) $(OBJ) $(LDFLAGS) $(LDLIBS)

%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

