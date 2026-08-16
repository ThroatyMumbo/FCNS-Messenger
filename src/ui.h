#ifndef UI_H
#define UI_H

#include <stdint.h>

/* Text into the RAM shadow buffer asm/nmi.s paints from. Nothing here touches
   $2006/$2007, and a glyph index is its ASCII code, because the font is
   uploaded to CHR $0200 = tile $20.

   Rows outside SH_ROW0..SH_ROW0+SH_ROWS-1 are not on screen; both values must
   match asm/nmi.s, which owns the buffer. */
#define SH_ROW0 2
#define SH_ROWS 25

void ui_clear(void);
void ui_clear_row(uint8_t row);
void ui_copy_row(uint8_t dst, uint8_t src);
void ui_putc(uint8_t col, uint8_t row, char c);
void ui_puts(uint8_t col, uint8_t row, const char *s);
void ui_puthex(uint8_t col, uint8_t row, uint8_t v);

/* Blocks until the NMI has run. This is the demo's only clock. */
void ui_wait_frame(void);

#endif
