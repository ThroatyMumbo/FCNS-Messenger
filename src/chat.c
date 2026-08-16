#include "chat.h"

#include "fcns.h"
#include "keypad.h"
#include "ui.h"

/* Rows: 4-18 the log, 19 a divider, 20 the line being composed, 22-25 the
   letter grid, 26 its action row. */
#define LOG_TOP   4
#define LOG_ROWS  15
#define DIV_ROW   19
#define EDIT_ROW  20
#define GRID_TOP  22
#define GRID_ROWS 4
#define ACT_ROW   GRID_ROWS /* one past the letters: [SP] [DEL] [SEND] */

#define GRID_W    10 /* letters across */
#define GRID_COL  2  /* column of the first letter */
#define GRID_STEP 3  /* columns between letters, leaving room for the [ ] */

/* clang-format off */
static const char grid[GRID_ROWS * GRID_W + 1] =
    "ABCDEFGHIJ"
    "KLMNOPQRST"
    "UVWXYZ0123"
    "456789.?!-";
/* clang-format on */

enum { ACT_SPACE, ACT_DEL, ACT_SEND, ACT_COUNT };

static const uint8_t     act_col[ACT_COUNT] = {2, 13, 24};
static const char *const act_txt[ACT_COUNT] = {"[SP]", "[DEL]", "[SEND]"};

static uint8_t gx, gy; /* the cursor, in grid cells */

static char    compose[MSG_MAX + 1] NOZP;
static uint8_t compose_len;

static uint8_t log_used;

static void draw_grid(void) {
    uint8_t r, c, i;

    for (r = 0; r < GRID_ROWS; r++) {
        ui_clear_row(GRID_TOP + r);
        for (c = 0; c < GRID_W; c++) {
            uint8_t col = GRID_COL + c * GRID_STEP;
            ui_putc(col, GRID_TOP + r, grid[r * GRID_W + c]);
            if (gy == r && gx == c) {
                ui_putc(col - 1, GRID_TOP + r, '[');
                ui_putc(col + 1, GRID_TOP + r, ']');
            }
        }
    }
    ui_clear_row(GRID_TOP + ACT_ROW);
    for (i = 0; i < ACT_COUNT; i++) {
        ui_puts(act_col[i], GRID_TOP + ACT_ROW, act_txt[i]);
        if (gy == ACT_ROW && gx == i)
            ui_putc(act_col[i] - 1, GRID_TOP + ACT_ROW, '>');
    }
}

static void draw_compose(void) {
    ui_clear_row(EDIT_ROW);
    ui_puts(1, EDIT_ROW, "SEND: ");
    ui_puts(7, EDIT_ROW, compose);
    ui_putc(7 + compose_len, EDIT_ROW, '_');
}

void chat_draw(void) {
    ui_puts(1, DIV_ROW, "------------------------------");
    draw_compose();
    draw_grid();
}

void chat_log(char dir, const char *s) {
    uint8_t r;

    if (log_used == LOG_ROWS) {
        for (r = 0; r < LOG_ROWS - 1; r++)
            ui_copy_row(LOG_TOP + r, LOG_TOP + r + 1);
        log_used = LOG_ROWS - 1;
    }
    ui_clear_row(LOG_TOP + log_used);
    ui_putc(1, LOG_TOP + log_used, dir);
    ui_puts(3, LOG_TOP + log_used, s);
    log_used++;
}

void chat_reset(void) {
    uint8_t r;

    compose_len = 0;
    compose[0] = '\0';
    gx = gy = 0;
    for (r = 0; r < LOG_ROWS; r++)
        ui_clear_row(LOG_TOP + r);
    log_used = 0;
}

const char *chat_line(void) { return compose; }

void chat_sent(void) {
    compose_len = 0;
    compose[0] = '\0';
    draw_compose();
}

static void add(char c) {
    if (compose_len < MSG_MAX) {
        compose[compose_len++] = c;
        compose[compose_len] = '\0';
    }
}

static void del(void) {
    if (compose_len != 0)
        compose[--compose_len] = '\0';
}

static void move(int8_t dx, int8_t dy) {
    uint8_t w;

    if (dy != 0) {
        if (dy < 0)
            gy = gy == 0 ? ACT_ROW : gy - 1;
        else
            gy = gy == ACT_ROW ? 0 : gy + 1;
    }
    w = gy == ACT_ROW ? ACT_COUNT : GRID_W;
    if (gx >= w)
        gx = w - 1;
    if (dx != 0) {
        if (dx < 0)
            gx = gx == 0 ? w - 1 : gx - 1;
        else
            gx = gx + 1 >= w ? 0 : gx + 1;
    }
}

static bool pick(void) {
    bool send = false;

    if (gy < GRID_ROWS)
        add(grid[gy * GRID_W + gx]);
    else if (gx == ACT_SPACE)
        add(' ');
    else if (gx == ACT_DEL)
        del();
    else
        send = compose_len != 0;
    draw_grid();
    draw_compose();
    return send;
}

bool chat_key(uint8_t key) {
    switch (key) {
    case KEY_UP:
        move(0, -1);
        draw_grid();
        break;
    case KEY_DOWN:
        move(0, 1);
        draw_grid();
        break;
    case KEY_LEFT:
        move(-1, 0);
        draw_grid();
        break;
    case KEY_RIGHT:
        move(1, 0);
        draw_grid();
        break;
    case KEY_EXEC:
        return pick();
    case KEY_INDEX:
        return compose_len != 0;
    case KEY_DOT:
        add(' ');
        draw_compose();
        break;
    case KEY_CLEAR:
        del();
        draw_compose();
        break;
    default:
        if (key >= '0' && key <= '9') {
            add((char)key);
            draw_compose();
        }
        break;
    }
    return false;
}
