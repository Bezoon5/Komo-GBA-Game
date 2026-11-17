    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       reset
    .fill   188, 1, 0          @ header (logo, title, checksum) filled by fixheader.py
reset:
    mov     r0, #0x12          @ IRQ mode
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F          @ System mode
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00
    @ copy .data/.iwram to IWRAM
    ldr     r0, =__data_lma
    ldr     r1, =__data_start
    ldr     r2, =__data_end
1:  cmp     r1, r2
    ldrlt   r3, [r0], #4
    strlt   r3, [r1], #4
    blt     1b
    @ clear .bss
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
    mov     r3, #0
2:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     2b
    @ clear EWRAM .ewram_bss
    ldr     r1, =__ewram_start
    ldr     r2, =__ewram_end
3:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     3b
    @ install IRQ handler
    ldr     r0, =0x03007FFC
    ldr     r1, =irq_handler
    str     r1, [r0]
    ldr     r0, =main
    mov     lr, pc
    bx      r0
4:  b       4b

    .global irq_handler
irq_handler:
    mov     r0, #0x04000000
    ldr     r1, [r0, #0x200]       @ IE | IF<<16
    and     r2, r1, r1, lsr #16    @ IE & IF
    add     r3, r0, #0x200
    strh    r2, [r3, #2]           @ acknowledge IF
    ldr     r3, =0x03007FF8
    ldrh    r1, [r3]
    orr     r1, r1, r2
    strh    r1, [r3]               @ BIOS IntrWait flags
    tst     r2, #4                 @ VCount: the black-and-white wipe line
    beq     5f
    ldr     r1, =split_pal
    ldr     r1, [r1]
    cmp     r1, #0
    beq     5f
    stmfd   sp!, {r4-r11}
    mov     r3, #0x05000000
    mov     r12, #32               @ 32 x 8 words = 1 KB, the whole BG + OBJ palette
6:  ldmia   r1!, {r4-r11}
    stmia   r3!, {r4-r11}
    subs    r12, r12, #1
    bne     6b
    ldmfd   sp!, {r4-r11}
5:  bx      lr
    .pool
