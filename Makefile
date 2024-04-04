prefix = /usr

CC = gcc
RM = rm -f

INSTALL = install -c
INSTALL_PRG = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

UNAME := $(shell uname)
UNAME_M := $(shell uname -m)


CFLAGS = -O3 \
         -funsafe-math-optimizations \
         $(shell sdl-config --cflags)

ifeq ($(UNAME_M),x86_64)
else
ifeq ($(UNAME_M),aarch64)
else
ifneq (,$(findstring arm, $UNAME_M))
CLAGS += -mfloat-abi=hard -mno-unaligned-access
endif

LDFLAGS = $(shell sdl-config --libs)
LDLIBS = -lhpdf -lpng
ifneq ($(UNAME), Darwin)
LDLIBS += -lrt
endif

PRG = printerToPDF
SRC = PrinterConvert.c
OBJ = $(SRC:%.c=%.o)
FNT = $(wildcard font2/*.C16) $(wildcard font2/*.D12)

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

