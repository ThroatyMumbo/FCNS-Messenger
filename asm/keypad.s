; The HVC-051 front-port keypad — never the hardwired pad. One strobe, then
; 24 clocks, taking both data lines. docs/PROTOCOL.md §6.

    .globl key_read_raw, key_held

JOY1 = 0x4016

    .bss
; Clocks 0-7 land in key_held+2, 8-15 in +1, 16-23 in +0, MSB first within a
; group — the layout the 23-key measurement is written against.
key_held:
    .fill 3, 1, 0
d0:
    .fill 3, 1, 0
d1:
    .fill 3, 1, 0

    .text
key_read_raw:
    lda #0x01
    sta JOY1
    lda #0x00
    sta JOY1

    ldx #0x02
.Lk_group:
    ldy #0x08
.Lk_bit:
    lda JOY1
    lsr a                       ; D0 -> C
    rol d0,x
    lsr a                       ; D1 -> C
    rol d1,x
    dey
    bne .Lk_bit
    dex
    bpl .Lk_group

    ; All-ones means an empty port. Test ONLY the two TRAILING groups (+0 and
    ; +1): clocks 0-7 read $00 legitimately with nothing pressed, so including
    ; that group stops the guard ever firing.
    lda d0+0
    and d0+1
    cmp #0xFF
    bne .Lk_d1
    lda #0x00
    sta d0+0
    sta d0+1
.Lk_d1:
    lda d1+0
    and d1+1
    cmp #0xFF
    bne .Lk_merge
    lda #0x00
    sta d1+0
    sta d1+1

.Lk_merge:
    ldx #0x02
.Lk_merge_loop:
    lda d0,x
    ora d1,x
    sta key_held,x
    dex
    bpl .Lk_merge_loop
    rts
