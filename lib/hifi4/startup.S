    .section .ResetVector.literal, "a"
    .align 4
.Lstart_addr:
    .word   _start

    .section .ResetVector.text, "ax"
    .global _ResetVector
    .align 4

_ResetVector:
    /* Load _start address from literal pool and jump (j can't reach DDR) */
    l32r    a0, .Lstart_addr
    jx      a0

    .text
    .align  4

    /* Literal pool for _start - must be BEFORE code that uses l32r */
    .literal_position
.Lvecbase:
    .word   _memmap_vecbase_reset
.Lcacheattr:
    .word   _memmap_cacheattr_reset

    .align  4
    .global _start
    .type   _start, @function

_start:
    /*
     * Xtensa Reset Handler - modeled after libhandler-reset (reset-vector-xea2.S)
     *
     * The order of operations is CRITICAL:
     * 1. Disable interrupts
     * 2. Set VECBASE
     * 3. Setup TLB cache attributes (BEFORE cache ops!)
     * 4. Invalidate I-cache
     * 5. Invalidate D-cache
     * 6. Set WindowStart THEN WindowBase (order matters!)
     * 7. Clear a0, setup stack
     * 8. Clear BSS
     * 9. Call main
     */

    /* Clear a0 and disable all interrupts */
    movi    a0, 0
    wsr.intenable a0            /* Disable all interrupts */

    /* Set interrupt vector base */
    l32r    a2, .Lvecbase
    wsr.vecbase a2

    /* Set interrupt level to 1 (like libhandler-reset does) */
    rsil    a2, 1


    /*
     * Invalidate entire I-cache
     * I-cache: 32KB, 64-byte lines, 2 ways
     * Loop 128 times, 4 lines per iteration = 512 total (covers all ways)
     */
    movi    a2, 0x4000          /* 32KB / 2 ways cache */
    movi    a3, 0
1:  iii     a3, 0
    iii     a3, 0x40
    iii     a3, 0x80
    iii     a3, 0xc0
    addmi   a3, a3, 0x100       /* a3 += 256 */
    bltu    a3, a2, 1b
    isync

    /*
     * Invalidate entire D-cache
     * D-cache: 32KB, 64-byte lines, 4 ways
     * Loop 128 times, 4 lines per iteration = 512 total (covers all ways)
     */
    movi    a2, 0x2000          /* 32KB / 4ways cache */
    movi    a3, 0
1:  dii     a3, 0
    dii     a3, 0x40
    dii     a3, 0x80
    dii     a3, 0xc0
    addmi   a3, a3, 0x100       /* a3 += 256 */
    bltu    a3, a2, 1b
    dsync

    /* TODO: do we need to setup prefetching here? */

    /*
     * Setup TLB cache attributes for all 8 x 512MB regions.
     * This MUST be done before cache invalidation on warm restart,
     * otherwise we might be operating with stale/wrong TLB entries.
     *
     * XCHAL_SPANNING_WAY = 0 for this config
     * Each region: read current TLB, preserve PPN, update cache attribute.
     */
    l32r    a7, .Lcacheattr     /* a7 = cache attributes (8 x 4-bit nibbles) */

    movi    a5, 0xE0000000      /* mask for upper 3 bits (512MB region) */
    movi    a6, 3f
    movi    a3, 0               /* XCHAL_SPANNING_WAY = 0 */
    and     a6, a6, a5          /* upper 3 bits of local area */
    j       3f                  /* jump to setup */

    /* Note the following instruction is aligned such that it fit
     * within one cache line.
     */
    .begin no-transform
1:  witlb   a4, a3              /* write ITLB entry safely */
    isync
    .end no-transform
    nop
    nop

    sub    a3, a3, a5          /* next region (add 0x20000000) */
    bltui  a3, 16, 4f          /* done */


    /* Setup ITLB for all 8 regions */
2:  srli    a7, a7, 4           /* shift to next nibble */
3:  ritlb1  a8, a3              /* read current ITLB entry (PPN + CA) */
    extui   a4, a7, 0, 4        /* extract lowest nibble (CA to set) */
    srli    a8, a8, 4           /* clear old CA, keep PPN */
    slli    a8, a8, 4
    add     a4, a4, a8          /* combine new CA with old PPN */
    beq     a3, a6, 1b          /* write in a safe way */
    witlb   a4, a3              /* write ITLB entry */
    sub     a3, a3, a5          /* next region (add 0x20000000) */
    bgeui   a3, 16, 2b          /* loop while a3 >= 8 (way bits) */
    isync                       /* make sure all ifetch changes take effect */

4:
    /*
     * Setup TLB dd-cache attributes for all 8 x 512MB regions.
     * This MUST be done before cache invalidation on warm restart,
     * otherwise we might be operating with stale/wrong TLB entries.
     *
     * XCHAL_SPANNING_WAY = 0 for this config
     * Each region: read current TLB, preserve PPN, update cache attribute.
     */
    l32r    a7, .Lcacheattr     /* a7 = cache attributes (8 x 4-bit nibbles) */

    movi    a5, 0xE0000000      /* mask for upper 3 bits (512MB region) */
    movi    a3, 0               /* XCHAL_SPANNING_WAY = 0 */

1:  rdtlb1  a8, a3              /* read current DTLB entry (PPN + CA) */
    extui   a4, a7, 0, 4        /* extract lowest nibble (CA to set) */
    srli    a8, a8, 4           /* clear old CA, keep PPN */
    slli    a8, a8, 4
    add     a4, a4, a8          /* combine new CA with old PPN */
    wdtlb   a4, a3              /* write DTLB entry */
    sub     a3, a3, a5          /* next region (add 0x20000000) */
    srli    a7, a7, 4           /* shift to next nibble */
    bgeui   a3, 16, 1b          /* loop while a3 >= 8 (way bits) */
    dsync                       /* make sure all data path changes take effect */



    /*
     * Initialize WindowStart and WindowBase
     * CRITICAL: Must write WindowStart BEFORE WindowBase (as libhandler-reset does)
     * Then ONE rsync after both, not two separate ones.
     * a0 is still 0 from the beginning
     */
    movi    a1, 1
    wsr.windowstart a1          /* WindowStart = 1 (only window 0 valid) */
    wsr.windowbase a0           /* WindowBase = 0 (a0 is still 0) */
    rsync

    /* Clear a0 again (belt and suspenders, like libhandler-reset) */
    movi    a0, 0

    /* Initialize PS register - DISABLE windowed calls for Call0 ABI */
    movi    a2, 0x00000000      /* PS.WOE=0, PS.INTLEVEL=0, PS.EXCM=0 */
    wsr.ps  a2
    rsync

    /* Clear loop registers to prevent unexpected loop behavior */
    movi    a2, 0
    wsr.lcount a2
    wsr.lbeg a2
    wsr.lend a2

    /* Clear shift amount register */
    wsr.sar a2

    /* Initialize stack pointer */
    movi    a1, __stack

    /* Clear BSS section BEFORE calling main */
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0           /* Value to write (zero) */
.Lbss_loop:
    bgeu    a2, a3, .Lbss_done
    s32i    a4, a2, 0
    addi    a2, a2, 4
    j       .Lbss_loop
.Lbss_done:

    /* Set return address to hang loop in case main returns */
    movi    a0, .Lhang

    /* Jump to main (using call0 - non-windowed call) */
    call0   main

    /* If main returns, loop forever */
.Lhang:
    call0   .Lhang
