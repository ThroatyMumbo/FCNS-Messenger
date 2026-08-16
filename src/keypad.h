#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

#define KEY_EXEC  0x42 /* 実行 / A button */
#define KEY_INDEX 0x45 /* 目次 / B button */
#define KEY_PREV  0x54 /* ◀ 前ページ / SELECT */
#define KEY_NEXT  0x55 /* ▶ 次ページ / START */
#define KEY_UP    0x51
#define KEY_DOWN  0x52
#define KEY_LEFT  0x50
#define KEY_RIGHT 0x53
#define KEY_STAR  0x2A
#define KEY_POUND 0x23
#define KEY_DOT   0x2E
#define KEY_CLEAR 0x44 /* C, like on a calculator */
#define KEY_END   0x46 /* 通信終了 / End call */

void    keypad_scan(void);
uint8_t keypad_pressed(void);

#endif
