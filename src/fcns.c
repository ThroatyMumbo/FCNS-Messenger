#include "fcns.h"

/* ---- the adapter ---------------------------------------------------------- */

#define REG_40A3 (*(volatile uint8_t *)0x40A3)
#define REG_40A8 (*(volatile uint8_t *)0x40A8)
#define REG_40AB (*(volatile uint8_t *)0x40AB)
#define REG_40AD (*(volatile uint8_t *)0x40AD)
#define REG_40AE (*(volatile uint8_t *)0x40AE)
#define REG_40B1 (*(volatile uint8_t *)0x40B1)
#define REG_40C0 (*(volatile uint8_t *)0x40C0)
#define REG_40D3 (*(volatile uint8_t *)0x40D3) /* the mailbox handshake */
#define REG_40D4 (*(volatile uint8_t *)0x40D4)

/* ---- the mailbox ---------------------------------------------------------- */

/* What cpu2_send/cpu2_recv read and write; the driver takes nothing in
   registers. Defined in asm/cpu2_drv.s, so the sizes must match it.
   `pad` is the byte CPU2 ignores; `errors` is the one it fills in. */
struct mbx_tx_t {
    uint8_t status, cmd, count, pad;
    uint8_t payload[80];
};

struct mbx_rx_t {
    uint8_t status, cmd, count, errors;
    uint8_t payload[252];
};

extern volatile struct mbx_tx_t mbx_tx;
extern volatile struct mbx_rx_t mbx_rx;

void    cpu2_send_raw(void);
void    cpu2_recv_raw(void);
uint8_t cpu2_send(void); /* returns mbx_tx.status */
uint8_t cpu2_recv(void); /* returns mbx_rx.status */

#define TX_OK   0x00
#define TX_BUSY 0xF2 /* mailbox never went idle; retry next pass */
#define RX_OK   0x00 /* $E2 is the ring-empty answer; the rest are timeouts */

#define RESP_SLOTS 13 /* CPU2's response ring, and so the drain bound */

/* Commands this demo issues. docs/PROTOCOL.md §2, §3 and §5. */
#define CMD_DIAL   0x00
#define CMD_HANGUP 0x01 /* mode 2 only; param $01 */
#define CMD_ANSWER 0x02
#define CMD_IDENT  0x03
#define CMD_TX     0x40 /* state 2 only */
#define CMD_CONFIG 0x60
#define CMD_ABORT  0x61 /* modes 1 and 4; raises no response */
#define CMD_AUDIO  0x65
#define CMD_COEF   0x69
#define CMD_PATCH  0x7D

/* Responses. Only $C0 is unsolicited in the middle of a call. */
#define RSP_DIALED   0x80
#define RSP_ANSWERED 0x82
#define RSP_HUNGUP   0x81
#define RSP_DATA     0xC0
#define RSP_LINELOST 0xE0 /* payload[0] 1 = the $4126 guard, 0 = $8B */

/* ---- what bring-up and a call are made of --------------------------------- */

/* $84 = $01 picks the six-entry ladder both sides use; $85 = $80 means CPU2
   contributes no application bytes, sends no COM and waits for none. */
static const uint8_t cfg_60[6] = {0x01, 0x80, 0x00, 0x00, 0x00, 0x00};

/* CPU2's own default block: $8A = $03 ETX framing, $8F = $FC cap. A 0 at $8C
   would disable both carrier checks. */
static const uint8_t coef_69[10] = {0x03, 0x04, 0x14, 0x0E, 0x00,
                                    0xFC, 0x00, 0x30, 0x26, 0x21};

/* JRA-PAT's $7D patch, but $0102 points at $E839: their $011A stub bumps the
   ladder step every NMI and runs a six-entry table off its end. */
/* clang-format off */
static const uint8_t patch_7d[41] = {
    0x77, 0x0A,
    0x01, 0x02,
    0x39, 0xE8, 0x09, 0x01, 0x06, 0xE0, 0xD5,
    0xA2, 0x90, 0xA5, 0x29, 0xF0, 0x06, 0xC9, 0xFF, 0xD0, 0x04,
    0xA2, 0x01, 0x86, 0x29, 0x4C, 0x20, 0xF4,
    0xAD, 0x32, 0x00, 0xC9, 0x05, 0xD0, 0x03, 0xEE, 0x32, 0x00, 0x4C, 0x39,
    0xE8};
/* clang-format on */

/* CPU2 dials a byte-code string, not digits: `B` waits for dial tone first. */
static const char dial_str[] = "B1 " FCNS_NUMBER;
#define DIAL_LEN (sizeof dial_str - 1)

#define ANS_TMO   0x03 /* ring-wait timeout, in 20-second units */
#define ANS_RINGS 0x02 /* two cadence-validated rings */

/* $03 is the first thing CPU2 answers, and it takes a few asks. */
#define ID_TRIES 20

/* fcns_poll() runs once a frame, so these are frames. The answer wait is the
   longer of the two deliberately: CPU2's own ring wait is 3 x 20 s. */
#define DIAL_WAIT   3600
#define ANSWER_WAIT 3900

/* ---- state ---------------------------------------------------------------- */

static link_state_t state = LINK_ABSENT; /* until fcns_init() says otherwise */
static uint16_t     waited;
static uint8_t      last_status;
static bool         mute_due;

static char    tx_pending[MSG_MAX + 1] NOZP;
static uint8_t tx_len;

static char    rx_line[MSG_MAX + 1] NOZP;
static uint8_t rx_len;
static bool    rx_ready;

uint8_t fcns_status(void) { return last_status; }

/* ---- bring-up ------------------------------------------------------------- */

/* This ordering is the adapter's own; it is not a sequence to tidy. */
static void hw_init(void) {
    REG_40A8 = 0x00;
    REG_40AB = 0x00;
    REG_40AD = 0x00; /* vertical mirroring */
    REG_40AE = 0x00;
    REG_40C0 = 0x00;
    REG_40A3 = 0x2F;
    REG_40B1 = 0xFF;
    REG_40D3 = 0xFF; /* the mailbox handshake, released */
    REG_40D4 = 0xFF;
    REG_40B1 = 0xF7; /* release CPU2 */
    REG_40C0 = 0x04;
    REG_40AE = 0x01; /* card SRAM at $6000 */
    REG_40C0 = 0x05;
}

/* The CIC gate, bounded at ~0.3 s. Whether it opens is also the answer to "is
   there an adapter", which is what decides whether there is a call to make. */
bool fcns_init(void) {
    uint16_t i;

    for (i = 0; i != 0xFFFF; i++) {
        if (REG_40C0 & 0x80) {
            hw_init();
            state = LINK_IDLE;
            return true;
        }
    }
    return false;
}

/* ---- talking to CPU2 ------------------------------------------------------ */

static void take(void);

/* CPU2 holds the mailbox while it has a response the card has not taken, so a
   send that has not drained first fails $F2. */
static void drain(void) {
    uint8_t i;

    for (i = 0; i < RESP_SLOTS; i++) {
        if (cpu2_recv() != RX_OK)
            return;
        take();
    }
}

static void payload(const uint8_t *src, uint8_t n) {
    uint8_t i;

    for (i = 0; i < n; i++)
        mbx_tx.payload[i] = src[i];
}

/* The payload is already in mbx_tx.payload; these put the header on it. Use
   mbx_send_drained() unless the ring has just been drained by hand. */
static uint8_t mbx_send(uint8_t cmd, uint8_t count) {
    mbx_tx.cmd = cmd;
    mbx_tx.count = count;
    mbx_tx.pad = 0;
    return cpu2_send();
}

static uint8_t mbx_send_drained(uint8_t cmd, uint8_t count) {
    drain();
    return mbx_send(cmd, count);
}

/* V and N both set is the mailbox idle; the same condition cpu2_send would
   otherwise spin on, so testing it first makes the driver fall straight
   through. */
static bool mbx_idle(void) { return (REG_40D3 & 0xC0) == 0xC0; }

uint8_t fcns_bringup(void) {
    uint8_t st = TX_BUSY, tries;

    for (tries = 0; tries < ID_TRIES && st != TX_OK; tries++)
        st = mbx_send_drained(CMD_IDENT, 0);
    if (st != TX_OK)
        return st;

    payload(patch_7d, sizeof patch_7d);
    st = mbx_send_drained(CMD_PATCH, sizeof patch_7d);
    if (st != TX_OK)
        return st;

    payload(coef_69, sizeof coef_69);
    return mbx_send_drained(CMD_COEF, sizeof coef_69);
}

/* ---- placing a call ------------------------------------------------------- */

/* $65 byte 0: $01 = /Phone Audio Enable on, $02 = off. The originate ladder
   mutes itself at step 1; the answer ladder has no mute in it at all. */
static uint8_t audio(uint8_t on) {
    mbx_tx.payload[0] = on;
    mbx_tx.payload[1] = 0x00;
    return mbx_send_drained(CMD_AUDIO, 2);
}

/* Both entries send $65 then $60 first: audio on so the handshake is audible,
   then the config that makes CPU2 a bare pipe. */
static uint8_t call_setup(void) {
    uint8_t st = audio(0x01);

    if (st != TX_OK)
        return st;
    payload(cfg_60, sizeof cfg_60);
    return mbx_send_drained(CMD_CONFIG, sizeof cfg_60);
}

uint8_t fcns_dial(void) {
    uint8_t st = call_setup();

    if (st != TX_OK)
        return st;

    payload((const uint8_t *)dial_str, DIAL_LEN);
    st = mbx_send_drained(CMD_DIAL, DIAL_LEN);
    if (st == TX_OK) {
        state = LINK_DIALING;
        waited = 0;
    }
    return st;
}

uint8_t fcns_answer(void) {
    uint8_t st = call_setup();

    if (st != TX_OK)
        return st;

    mbx_tx.payload[0] = ANS_TMO;
    mbx_tx.payload[1] = ANS_RINGS;
    st = mbx_send_drained(CMD_ANSWER, 2);
    if (st == TX_OK) {
        state = LINK_RINGWAIT;
        waited = 0;
    }
    return st;
}

/* $61 raises no response and is legal while dialing; $01 is mode-2 only and
   would queue an $E1 nobody drains. Neither substitutes for the other, so the
   drain comes first: an $E0 in the ring means CPU2 has already torn down and
   there is nothing left to send. */
uint8_t fcns_hangup(void) {
    uint8_t st = TX_OK;

    drain();
    if (state == LINK_UP) {
        mbx_tx.payload[0] = 0x01;
        st = mbx_send(CMD_HANGUP, 1);
    } else if (state == LINK_DIALING || state == LINK_RINGWAIT) {
        st = mbx_send(CMD_ABORT, 0);
    }
    state = LINK_IDLE;
    tx_len = 0;
    rx_len = 0;
    rx_ready = false;
    return st;
}

/* ---- messages ------------------------------------------------------------- */

bool fcns_send(const char *s) {
    uint8_t n = 0;

    if (state != LINK_UP || tx_len != 0)
        return false;
    while (s[n] != '\0' && n < MSG_MAX) {
        tx_pending[n] = s[n];
        n++;
    }
    if (n == 0)
        return false;
    tx_pending[n++] = MSG_ETX;
    tx_len = n;
    return true;
}

uint8_t fcns_recv(char *buf, uint8_t max) {
    uint8_t i, n;

    if (!rx_ready)
        return 0;
    n = rx_len < max ? rx_len : max;
    for (i = 0; i < n; i++)
        buf[i] = rx_line[i];
    buf[n] = '\0';
    rx_ready = false;
    rx_len = 0;
    return n;
}

/* Rx comes up before the far end finishes training, so the first records are
   line noise. Our payload is printable text; anything else is dropped. */
static bool printable(uint8_t c) { return c >= 0x20 && c <= 0x7E; }

static void take_data(void) {
    uint8_t n = mbx_rx.count;
    uint8_t i, start = 0;

    if (n == 0 || n > sizeof mbx_rx.payload)
        return;
    if (mbx_rx.payload[n - 1] != MSG_ETX)
        return;
    n--;

    /* Training junk carries no terminator of its own, so it shares a record
       with the first real message. Keep the printable tail, not nothing. */
    for (i = 0; i < n; i++)
        if (!printable(mbx_rx.payload[i]))
            start = i + 1;
    if (start >= n)
        return;
    if (n - start > MSG_MAX)
        start = n - MSG_MAX;

    for (i = start; i < n; i++)
        rx_line[i - start] = (char)mbx_rx.payload[i];
    rx_len = n - start;
    rx_line[rx_len] = '\0';
    rx_ready = true;
}

/* ---- the pass ------------------------------------------------------------- */

static void take(void) {
    switch (mbx_rx.cmd) {
    case RSP_DIALED:
    case RSP_ANSWERED:
        last_status = mbx_rx.payload[0];
        state = last_status == 0 ? LINK_UP : LINK_FAILED;
        if (state == LINK_UP)
            mute_due = true;
        break;
    case RSP_HUNGUP:
        last_status = mbx_rx.payload[0];
        state = LINK_IDLE;
        break;
    case RSP_LINELOST:
        last_status = mbx_rx.payload[0];
        state = LINK_IDLE;
        tx_len = 0;
        rx_len = 0;
        rx_ready = false;
        break;
    case RSP_DATA:
        take_data();
        break;
    default:
        break;
    }
}

link_state_t fcns_poll(void) {
    if (state == LINK_ABSENT)
        return state;

    drain();

    if (state == LINK_DIALING || state == LINK_RINGWAIT) {
        uint16_t limit = state == LINK_DIALING ? DIAL_WAIT : ANSWER_WAIT;
        if (++waited >= limit) {
            /* $61 is the abort that is legal here; $01 is not. */
            mbx_send_drained(CMD_ABORT, 0);
            state = LINK_FAILED;
            last_status = STAT_TIMEOUT;
            return state;
        }
    }

    /* The answer ladder carries no mute of its own, so this is what stops the
       screech running for the whole session. */
    if (mute_due) {
        if (audio(0x02) == TX_OK)
            mute_due = false;
        return state;
    }

    /* $40 is state-2 only, and a $F2'd packet stays queued for the next pass
       rather than being retried inside this one. */
    if (state == LINK_UP && tx_len != 0 && mbx_idle()) {
        payload((const uint8_t *)tx_pending, tx_len);
        if (mbx_send(CMD_TX, tx_len) == TX_OK)
            tx_len = 0;
    }

    return state;
}
