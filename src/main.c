#include <ines.h>

#include "chat.h"
#include "fcns.h"
#include "keypad.h"
#include "ppu.h"
#include "ui.h"

MAPPER_PRG_ROM_KB(128);
MAPPER_CHR_ROM_KB(0);
MAPPER_CHR_RAM_KB(8);
MAPPER_USE_VERTICAL_MIRRORING;

#define TITLE_ROW  2
#define STATUS_COL 18
#define HINT_TOP   12

/* App states */
#define ST_DIALING    "DIALING"
#define ST_WAITING    "WAITING"
#define ST_READY      "READY"
#define ST_END_CALL   "ENDED"
#define ST_FAILED     "FAILED"
#define ST_CONNECTED  "CONNECTED"
#define ST_NO_ADAPTER "NO ADAPTER"
#define ST_CPU2_FAIL  "CPU2 FAIL"

static char inbox[MSG_MAX + 1] NOZP;

static const char *status_txt = "";

static void status(const char *s) {
    uint8_t i;

    status_txt = s;
    for (i = STATUS_COL; i < 32; i++)
        ui_putc(i, TITLE_ROW, ' ');
    ui_puts(STATUS_COL, TITLE_ROW, s);
}

/* A byte after the status word: the mailbox status that stopped bring-up, or
   why the last call ended. */
static void status_code(uint8_t code) { ui_puthex(28, TITLE_ROW, code); }

/* A failed call left CPU2 in mode 0, same as never having called at all, so
   dialing out of LINK_FAILED needs no clearing step first. */
static bool can_call(link_state_t st) {
    return st == LINK_IDLE || st == LINK_FAILED;
}

static void draw_hints(link_state_t st) {
    ui_clear_row(HINT_TOP);
    ui_clear_row(HINT_TOP + 1);
    ui_clear_row(HINT_TOP + 2);
    if (st == LINK_ABSENT)
        return;
    if (can_call(st)) {
        ui_puts(8, HINT_TOP, "START  = DIAL");
        ui_puts(8, HINT_TOP + 2, "SELECT = ANSWER");
    } else {
        ui_puts(14, HINT_TOP + 1, "...");
    }
}

typedef enum { SCR_IDLE, SCR_CALL } screen_t;

static screen_t screen;

static void draw_screen(link_state_t st) {
    ui_clear();
    ui_puts(1, TITLE_ROW, "FCNS MESSENGER");
    ui_puts(1, TITLE_ROW + 1, "------------------------------");
    ui_puts(STATUS_COL, TITLE_ROW, status_txt);
    if (screen == SCR_IDLE)
        draw_hints(st);
    else
        chat_draw();
}

static void idle_key(uint8_t key, link_state_t st) {
    switch (key) {
    case KEY_NEXT:
        if (can_call(st)) {
            fcns_dial();
            status(ST_DIALING);
        }
        break;
    case KEY_PREV:
        if (can_call(st)) {
            fcns_answer();
            status(ST_WAITING);
        }
        break;
    case KEY_END:
        if (st != LINK_ABSENT) {
            fcns_hangup();
            status(ST_END_CALL);
        }
        break;
    default:
        break;
    }
}

static void call_key(uint8_t key) {
    if (key == KEY_END) {
        fcns_hangup();
        status(ST_END_CALL);
    } else if (chat_key(key) && fcns_send(chat_line())) {
        chat_log('>', chat_line());
        chat_sent();
    }
}

int main(void) {
    link_state_t st, shown;
    screen_t     want;
    uint8_t      key, code;

    st = fcns_init() ? LINK_IDLE : LINK_ABSENT;
    ppu_init();

    screen = SCR_IDLE;
    draw_screen(st);
    ppu_on();

    code = st == LINK_ABSENT ? 0 : fcns_bringup();
    if (st == LINK_ABSENT) {
        status(ST_NO_ADAPTER);
    } else if (code != 0) {
        status(ST_CPU2_FAIL);
        status_code(code);
    } else {
        status(ST_READY);
    }
    shown = st;

    for (;;) {
        ui_wait_frame();
        keypad_scan();
        key = keypad_pressed();

        st = fcns_poll();

        if (st != shown) {
            if (st == LINK_UP) {
                status(ST_CONNECTED);
            } else if (st == LINK_FAILED) {
                status(ST_FAILED);
                status_code(fcns_status());
            } else if (shown == LINK_UP) {
                status(ST_END_CALL);
            }
        }

        want = st == LINK_UP ? SCR_CALL : SCR_IDLE;
        if (want != screen) {
            screen = want;
            if (screen == SCR_CALL)
                chat_reset();
            draw_screen(st);
        } else if (screen == SCR_IDLE && st != shown) {
            draw_hints(st);
        }
        shown = st;

        if (fcns_recv(inbox, MSG_MAX) != 0)
            chat_log('<', inbox);

        if (screen == SCR_IDLE)
            idle_key(key, st);
        else
            call_key(key);
    }
}
