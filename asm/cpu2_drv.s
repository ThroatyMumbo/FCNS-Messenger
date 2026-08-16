; The CPU2 mailbox driver. Send waits for $40D3 b7 to MATCH, receive for it to
; DIFFER; every bailout releases $40D3. docs/PROTOCOL.md §1 says why.

    .globl cpu2_send_raw, cpu2_recv_raw, cpu2_send, cpu2_recv
    .globl mbx_busy                 ; defined in nmi.s
    .globl mbx_tx, mbx_rx

REG_40D0 = 0x40D0
REG_40D1 = 0x40D1
REG_40D2 = 0x40D2
REG_40D3 = 0x40D3

TIMEOUT_OUTER = 0x40

; The ca65 original put these at $0600/$0700 because its linker config reserved
; them. CPU2 never sees the addresses, so let the linker place them.
TX_PAYLOAD_MAX = 80             ; longest we send is the 41-byte $7D patch
RX_PAYLOAD_MAX = 252            ; longest CPU2 can emit under $8F = $FC

    .bss
mbx_tx:
    .fill 4 + TX_PAYLOAD_MAX
mbx_rx:
    .fill 4 + RX_PAYLOAD_MAX

; Field order matches struct mbx_tx_t / mbx_rx_t in src/fcns.h.
TX_STATUS  = mbx_tx + 0
TX_CMD     = mbx_tx + 1
TX_COUNT   = mbx_tx + 2
TX_PAD     = mbx_tx + 3         ; sent, but CPU2 ignores it
TX_PAYLOAD = mbx_tx + 4

RX_STATUS  = mbx_rx + 0
RX_CMD     = mbx_rx + 1
RX_COUNT   = mbx_rx + 2
RX_ERRORS  = mbx_rx + 3         ; framing errors in b7-b4, parity in b3-b0
RX_PAYLOAD = mbx_rx + 4

; The ca65 original's $19/$1E/$28-$2A sit on llvm-mos's imaginary registers.
    .section .zp.bss,"aw",@nobits
ZP_HSHAKE:
    .fill 1
DRV_TIMEOUT:
    .fill 1
DRV_OFFSET:
    .fill 1
DRV_CHUNK:
    .fill 1

    .text

; The driver answers in its buffers, so these only lift the status into A.
; mbx_busy keeps the NMI off the PPU meanwhile; the deadline is ~435 ms.
cpu2_send:
    lda #0x80
    sta mbx_busy
    jsr cpu2_send_raw
    lda #0x00
    sta mbx_busy
    lda TX_STATUS
    rts

cpu2_recv:
    lda #0x80
    sta mbx_busy
    jsr cpu2_recv_raw
    lda #0x00
    sta mbx_busy
    lda RX_STATUS
    rts

; cpu2_send_raw — card -> CPU2. Waits for $40D3 b7 to MATCH the card's.
cpu2_send_raw:
    lda #0x80
    sta TX_STATUS
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Ls_wait_entry:
    ldy #0x00
.Ls_inner_entry:
    bit REG_40D3
    bvc .Ls_entry_busy
    bmi .Ls_entry_ready
.Ls_entry_busy:
    dey
    bne .Ls_inner_entry
    dec DRV_TIMEOUT
    bne .Ls_wait_entry
    lda #0xF2
    sta TX_STATUS
    rts
.Ls_entry_ready:
    lda #0x7F
    sta REG_40D3
    bit REG_40D3
    bvc .Ls_fail_F3
    bpl .Ls_fail_F3
    lda #0x3F
    sta REG_40D3
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Ls_wait_data_ack:
    ldy #0x00
.Ls_inner_data_ack:
    bit REG_40D3
    bpl .Ls_do_chunk0
    dey
    bne .Ls_inner_data_ack
    dec DRV_TIMEOUT
    bne .Ls_wait_data_ack
    lda #0xF4
    sta TX_STATUS
    lda #0xFF                   ; $3F left held would strand CPU2 in $FC26
    sta REG_40D3
    rts
.Ls_fail_F3:
    lda #0xFF
    sta REG_40D3
    lda #0xF3
    sta TX_STATUS
    rts
.Ls_do_chunk0:
    lda TX_CMD
    sta REG_40D0
    lda TX_COUNT
    sta REG_40D1
    lda TX_PAD
    sta REG_40D2
    lda #0xBF
    sta REG_40D3
    sta ZP_HSHAKE
    lda TX_COUNT
    beq .Ls_release
    lda #0x00
    sta DRV_OFFSET
.Ls_payload_next:
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Ls_wait_payload_ack:
    ldy #0x00
.Ls_inner_payload_ack:
    lda ZP_HSHAKE
    and #0x80
    sta DRV_CHUNK
    lda REG_40D3
    and #0x80
    cmp DRV_CHUNK
    beq .Ls_do_payload          ; MATCH, not differ — PROTOCOL.md §1
    dey
    bne .Ls_inner_payload_ack
    dec DRV_TIMEOUT
    bne .Ls_wait_payload_ack
    lda #0xF5
    sta TX_STATUS
    lda #0xFF                   ; leaving it at $BF is what turns our timeout
    sta REG_40D3                ; into CPU2's reason $02
    rts
.Ls_do_payload:
    ldx DRV_OFFSET
    lda TX_PAYLOAD,x
    sta REG_40D0
    inx
    cpx TX_COUNT
    bcs .Ls_short_1
    lda TX_PAYLOAD,x
    sta REG_40D1
    inx
    cpx TX_COUNT
    bcs .Ls_short_2
    lda TX_PAYLOAD,x
    sta REG_40D2
    inx
    jmp .Ls_payload_done
.Ls_short_1:
    sta REG_40D1
    sta REG_40D2
    jmp .Ls_payload_done
.Ls_short_2:
    sta REG_40D2
    inx
.Ls_payload_done:
    stx DRV_OFFSET
    lda ZP_HSHAKE
    eor #0x80
    sta ZP_HSHAKE
    sta REG_40D3
    lda DRV_OFFSET
    cmp TX_COUNT
    bcc .Ls_payload_next
.Ls_release:
    ; Let CPU2 mirror the last chunk before letting go: a straight $FF raises b6
    ; while it is still waiting, which $FC19's BVS turns into reason $03.
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Ls_wait_release:
    ldy #0x00
.Ls_inner_release:
    lda ZP_HSHAKE
    and #0x80
    sta DRV_CHUNK
    lda REG_40D3
    and #0x80
    cmp DRV_CHUNK
    beq .Ls_do_release
    dey
    bne .Ls_inner_release
    dec DRV_TIMEOUT
    bne .Ls_wait_release
    lda #0xF6                   ; CPU2 never mirrored the final chunk
    sta TX_STATUS
    lda #0xFF
    sta REG_40D3
    rts
.Ls_do_release:
    lda #0xFF
    sta REG_40D3
    lda #0x00
    sta TX_STATUS
    rts

; cpu2_recv_raw — CPU2 -> card. Waits for $40D3 b7 to DIFFER from the card's.
; The empty-ring answer is one register read and an rts, so polling is cheap.
cpu2_recv_raw:
    lda #0x80
    sta RX_STATUS
    bit REG_40D3
    bvs .Lr_no_data
    bmi .Lr_no_data
    lda #0x7F
    sta REG_40D3
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Lr_wait_c0:
    ldy #0x00
.Lr_inner_c0:
    bit REG_40D3
    bmi .Lr_read_c0
    dey
    bne .Lr_inner_c0
    dec DRV_TIMEOUT
    bne .Lr_wait_c0
    lda #0xE3
    sta RX_STATUS
    lda #0xFF
    sta REG_40D3
    rts
.Lr_no_data:
    lda #0xE2
    sta RX_STATUS
    rts
.Lr_read_c0:
    lda REG_40D0
    sta RX_CMD
    lda REG_40D1
    sta RX_COUNT
    lda REG_40D2
    sta RX_ERRORS
    lda #0xFF
    sta REG_40D3
    sta ZP_HSHAKE
    lda RX_COUNT
    beq .Lr_finalize
    lda #0x00
    sta DRV_OFFSET
.Lr_recv_next:
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Lr_wait_recv_ack:
    ldy #0x00
.Lr_inner_recv_ack:
    lda ZP_HSHAKE
    and #0x80
    sta DRV_CHUNK
    lda REG_40D3
    and #0x80
    cmp DRV_CHUNK
    bne .Lr_do_recv
    dey
    bne .Lr_inner_recv_ack
    dec DRV_TIMEOUT
    bne .Lr_wait_recv_ack
    lda #0xE4
    sta RX_STATUS
    lda #0xFF
    sta REG_40D3
    rts
.Lr_do_recv:
    ldx DRV_OFFSET
    lda REG_40D0
    sta RX_PAYLOAD,x
    inx
    cpx RX_COUNT
    bcs .Lr_chunk_end
    lda REG_40D1
    sta RX_PAYLOAD,x
    inx
    cpx RX_COUNT
    bcs .Lr_chunk_end
    lda REG_40D2
    sta RX_PAYLOAD,x
    inx
.Lr_chunk_end:
    stx DRV_OFFSET
    lda ZP_HSHAKE
    eor #0x80
    sta ZP_HSHAKE
    sta REG_40D3
    lda DRV_OFFSET
    cmp RX_COUNT
    bcc .Lr_recv_next
.Lr_finalize:
    lda #TIMEOUT_OUTER
    sta DRV_TIMEOUT
.Lr_wait_fin:
    ldy #0x00
.Lr_inner_fin:
    bit REG_40D3
    bpl .Lr_fin_not_yet
    bvc .Lr_fin_not_yet
    jmp .Lr_do_fin
.Lr_fin_not_yet:
    dey
    bne .Lr_inner_fin
    dec DRV_TIMEOUT
    bne .Lr_wait_fin
    lda #0xE5
    sta RX_STATUS
    lda #0xFF
    sta REG_40D3
    rts
.Lr_do_fin:
    lda #0xFF
    sta REG_40D3
    lda #0x00
    sta RX_STATUS
    rts
