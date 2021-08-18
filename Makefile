CC = /opt/sdcc-3.9.0/bin/sdcc
EXEC = bootloader.ihx
SRCC = bootloader.c
OBJ = $(SRCC:.c=.rel)
CFLAGS = -mmcs51 $(OPTCFLAGS)
LDFLAGS = -mmcs51 --xram-loc 0x0000 --xram-size 0x8000 --code-loc 0x0000

all: $(EXEC)

install: $(EXEC)
	minipro -p AT28C256 -f ihex -w $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $(EXEC) $(LDFLAGS)

%.rel: %.c
	$(CC) -c $< $(CFLAGS)

clean:
	rm -f $(EXEC) $(OBJ) *.asm *.sym *.map *.mem *.lk *.rst *.lst
