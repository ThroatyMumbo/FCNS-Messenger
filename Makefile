LLVM_MOS ?= $(HOME)/dev/llvm-mos
CC       := $(LLVM_MOS)/bin/mos-nes-mmc1-clang

TARGET  := demo.nes
# -fno-lto on purpose: inlining grows the static zero-page frame past 224 bytes.
# -MMD -MP so a header edit rebuilds what included it; without it a stale object
# kept a struct at the address a newer header had already moved.
CFLAGS  := -Os -Isrc -Wall -fno-lto -MMD -MP
LDFLAGS :=

CSRC := src/main.c src/chat.c src/fcns.c src/keypad.c src/ppu.c src/ui.c
ASRC := asm/cpu2_drv.s asm/keypad.s asm/nmi.s asm/font.s

OBJ := $(CSRC:.c=.o) $(ASRC:.s=.o)
DEP := $(CSRC:.c=.d)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) -c $< -o $@

asm/font.o: asm/font.chr

-include $(DEP)

clean:
	rm -f $(OBJ) $(DEP) $(TARGET) $(TARGET).elf demo.mlb

.PHONY: all clean
