#include "ui.h"

#define COLS 32

extern uint8_t shadow[SH_ROWS * COLS]; /* asm/nmi.s */

/* Only the low byte is read, so there is no torn 16-bit read; volatile keeps
   the wait loop from being hoisted away. */
extern volatile uint8_t frame_count[2];

/* A row outside the panel would run off the end of shadow and quietly eat
   whatever the linker put next. Park it on the last row instead. */
static uint8_t *row_ptr(uint8_t row) {
    uint8_t r = row - SH_ROW0;

    if (row < SH_ROW0 || r >= SH_ROWS)
        r = SH_ROWS - 1;
    return &shadow[r * COLS];
}

void ui_clear(void) {
    uint16_t i;

    for (i = 0; i < sizeof shadow; i++)
        shadow[i] = ' ';
}

void ui_clear_row(uint8_t row) {
    uint8_t *p = row_ptr(row);
    uint8_t  i;

    for (i = 0; i < COLS; i++)
        p[i] = ' ';
}

void ui_copy_row(uint8_t dst, uint8_t src) {
    uint8_t *d = row_ptr(dst);
    uint8_t *s = row_ptr(src);
    uint8_t  i;

    for (i = 0; i < COLS; i++)
        d[i] = s[i];
}

void ui_putc(uint8_t col, uint8_t row, char c) {
    if (col < COLS)
        row_ptr(row)[col] = (uint8_t)c;
}

void ui_puts(uint8_t col, uint8_t row, const char *s) {
    uint8_t *p = row_ptr(row);

    while (*s != '\0' && col < COLS)
        p[col++] = (uint8_t)*s++;
}

void ui_puthex(uint8_t col, uint8_t row, uint8_t v) {
    static const char hex[] = "0123456789ABCDEF";

    ui_putc(col, row, hex[v >> 4]);
    ui_putc(col + 1, row, hex[v & 0x0F]);
}

/* The software counter the NMI maintains, never $2002: the handler reads and
   clears that flag before the main loop could see it. */
void ui_wait_frame(void) {
    uint8_t t = frame_count[0];

    while (frame_count[0] == t)
        ;
}
