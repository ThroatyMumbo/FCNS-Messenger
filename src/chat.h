#ifndef CHAT_H
#define CHAT_H

#include <stdbool.h>
#include <stdint.h>

/* The call screen: a scrolling message log, the line being composed, and the
   letter grid that composes it. It owns screen rows 4 to 26 and knows nothing
   about the link — main.c does the sending. */

void chat_reset(void); /* empty it, for a new call */
void chat_draw(void);  /* repaint all of it */

void chat_log(char dir, const char *s); /* '<' arrived, '>' sent */

/* Feed it a keypad code. True means the user asked to send chat_line(); call
   chat_sent() once it has actually gone out. */
bool        chat_key(uint8_t key);
const char *chat_line(void);
void        chat_sent(void);

#endif
