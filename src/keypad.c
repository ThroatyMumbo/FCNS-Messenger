#include "keypad.h"

extern uint8_t key_held[3]; /* asm/keypad.s */
void           key_read_raw(void);

/* clang-format off */
static const uint8_t code_by_clock[24] = {
    KEY_EXEC,  KEY_INDEX, KEY_PREV, KEY_NEXT,  /*  0- 3 */
    KEY_UP,    KEY_DOWN,  KEY_LEFT, KEY_RIGHT, /*  4- 7 */
    '0', '1', '2', '3', '4', '5', '6', '7',    /*  8-15 */
    '8', '9',                                  /* 16-17 */
    KEY_STAR,  KEY_POUND, KEY_DOT,  KEY_CLEAR, /* 18-21 */
    0,         KEY_END                         /* 22-23 */
};
/* clang-format on */

static uint8_t prev[3];

void keypad_scan(void) { key_read_raw(); }

uint8_t keypad_pressed(void) {
    uint8_t grp, bit, edge, code = 0;
    uint8_t clock = 0;
    uint8_t i;

    for (i = 0; i < 3; i++) {
        grp = 2 - i;
        edge = (uint8_t)(key_held[grp] & ~prev[grp]);
        for (bit = 0; bit < 8; bit++) {
            if (code == 0 && (edge & (0x80 >> bit)))
                code = code_by_clock[clock];
            clock++;
        }
    }
    for (i = 0; i < 3; i++)
        prev[i] = key_held[i];
    return code;
}
