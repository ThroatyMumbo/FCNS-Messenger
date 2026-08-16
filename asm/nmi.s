; The NMI: a fixed-cost painter — watchdog, frame counter, a few shadow rows.
; Overrides the SDK's weak `nmi`, so the register saves are ours to place; the
; busy path touches no register, so a transaction costs only watchdog and clock.

    .globl nmi, frame_count, mbx_busy, shadow

REG_40AC = 0x40AC       ; unread for 12.4892 s and the RF5C66 pulses $4126.2

PPU_STATUS = 0x2002
PPU_SCROLL = 0x2005
PPU_ADDR   = 0x2006
PPU_DATA   = 0x2007

SH_ROW0   = 2           ; first shadow row on screen  } must match
SH_ROWS   = 25          ; rows 2..26, overscan-safe   } src/ui.h
; ~620 cycles a row against ~2273 of vblank: two fit, four overran into active
; rendering and scattered the address latch. Whole panel in 12 frames.
SH_PERPAS = 2

    .section .zp.bss,"aw",@nobits
frame_count:
    .fill 2
mbx_busy:
    .fill 1
sh_rownext:
    .fill 1
ptr:
    .fill 2
cnt:
    .fill 1

    .bss
shadow:
    .fill SH_ROWS * 32

    .text
nmi:
    bit REG_40AC
    inc frame_count
    bne .Ln_busytest
    inc frame_count+1
.Ln_busytest:
    bit mbx_busy
    bmi .Ln_out
    pha
    txa
    pha
    tya
    pha
    jsr nmi_render
    pla
    tay
    pla
    tax
    pla
.Ln_out:
    rti

nmi_render:
    lda #SH_PERPAS
    sta cnt
.Ln_row:
    ; PPU address of screen row R = SH_ROW0 + sh_rownext, i.e. $2000 + R*32.
    lda sh_rownext
    clc
    adc #SH_ROW0
    tax
    bit PPU_STATUS
    txa
    lsr a
    lsr a
    lsr a
    clc
    adc #0x20                   ; high byte: $20 + R>>3
    sta PPU_ADDR
    txa
    and #0x07
    asl a
    asl a
    asl a
    asl a
    asl a                       ; low byte: (R&7)<<5
    sta PPU_ADDR

    ; ptr = shadow + sh_rownext * 32
    lda sh_rownext
    sta ptr
    lda #0x00
    sta ptr+1
    ldx #0x05
.Ln_shift:
    asl ptr
    rol ptr+1
    dex
    bne .Ln_shift
    lda ptr
    clc
    adc #<shadow
    sta ptr
    lda ptr+1
    adc #>shadow
    sta ptr+1

    ldy #0x00
.Ln_byte:
    lda (ptr),y
    sta PPU_DATA
    iny
    cpy #32
    bne .Ln_byte

    inc sh_rownext
    lda sh_rownext
    cmp #SH_ROWS
    bcc .Ln_more
    lda #0x00
    sta sh_rownext
.Ln_more:
    dec cnt
    bne .Ln_row

    ; Mandatory after any $2006/$2007 batch, or v becomes the scroll origin.
    bit PPU_STATUS
    lda #0x00
    sta PPU_SCROLL
    sta PPU_SCROLL
    rts
