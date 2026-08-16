#ifndef FCNS_H
#define FCNS_H

#include <stdbool.h>
#include <stdint.h>

/* Change the phone number here */
#ifndef FCNS_NUMBER
#define FCNS_NUMBER "15555555555"
#endif

#define MSG_MAX      64   /* Max message length in bytes */
#define MSG_ETX      0x03 /* Rx terminator seeded into CPU2 $8A */
#define STAT_TIMEOUT 0xFF

/* Zero page is 224 bytes and llvm-mos promotes hot globals into it eagerly.
   Buffers this size belong in ordinary RAM, or the link overflows it. */
#define NOZP __attribute__((section(".bss")))

typedef enum {
    LINK_ABSENT,   /* the CIC gate never opened: no adapter */
    LINK_IDLE,     /* no call */
    LINK_DIALING,  /* $00 sent, waiting for $80 */
    LINK_RINGWAIT, /* $02 sent, waiting for $82 */
    LINK_UP,       /* CPU2 mode 2: data mode */
    LINK_FAILED    /* the far end never answered, or CPU2 refused */
} link_state_t;

bool         fcns_init(void);
uint8_t      fcns_bringup(void);
uint8_t      fcns_dial(void);
uint8_t      fcns_answer(void);
uint8_t      fcns_hangup(void);
link_state_t fcns_poll(void);
uint8_t      fcns_status(void);
bool         fcns_send(const char *s);
uint8_t      fcns_recv(char *buf, uint8_t max);

#endif
