; 96 tiles, ASCII $20-$7F, 1bpp in plane 0 so the palette picks the color.
; Uploaded to CHR $0200 so that glyph index == ASCII.

    .globl font_data

    .section .rodata,"a",@progbits
font_data:
    .incbin "asm/font.chr"
