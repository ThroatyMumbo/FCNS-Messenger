#include <stdint.h>

#include "ppu.h"

#define PPU_CTRL   (*(volatile uint8_t *)0x2000)
#define PPU_MASK   (*(volatile uint8_t *)0x2001)
#define PPU_STATUS (*(volatile uint8_t *)0x2002)
#define OAM_ADDR   (*(volatile uint8_t *)0x2003)
#define OAM_DATA   (*(volatile uint8_t *)0x2004)
#define PPU_ADDR   (*(volatile uint8_t *)0x2006)
#define PPU_DATA   (*(volatile uint8_t *)0x2007)

extern const uint8_t font_data[]; /* asm/font.s: 96 tiles, ASCII $20-$7F */

#define FONT_BYTES 1536

static void wait_vblank(void) {
    while (!(PPU_STATUS & 0x80))
        ;
}

static void upload_font(void) {
    uint16_t i;

    PPU_ADDR = 0x00;
    PPU_ADDR = 0x00;
    for (i = 0; i < 0x200; i++)
        PPU_DATA = 0x00;
    for (i = 0; i < FONT_BYTES; i++)
        PPU_DATA = font_data[i];
}

void ppu_init(void) {
    uint16_t i;

    wait_vblank(); /* the PPU ignores writes until two frames after power-on */
    wait_vblank();

    /* All 32 entries: only $3F00-$3F03 are used, but a stray attribute or
       sprite would otherwise show power-on garbage. */
    PPU_ADDR = 0x3F;
    PPU_ADDR = 0x00;
    for (i = 0; i < 32; i++)
        PPU_DATA = 0x0F;
    PPU_ADDR = 0x3F;
    PPU_ADDR = 0x01;
    PPU_DATA = 0x30;
    PPU_DATA = 0x10;
    PPU_DATA = 0x00;

    /* OAM is garbage at power-on and the demo uses no sprites. */
    OAM_ADDR = 0x00;
    for (i = 0; i < 256; i++)
        OAM_DATA = 0xFF;

    upload_font();

    PPU_ADDR = 0x20;
    PPU_ADDR = 0x00;
    for (i = 0; i < 0x3C0; i++)
        PPU_DATA = ' ';
    for (i = 0; i < 0x40; i++)
        PPU_DATA = 0x00;
}

void ppu_on(void) {
    PPU_CTRL = 0x80;
    PPU_MASK = 0x0A;
}
