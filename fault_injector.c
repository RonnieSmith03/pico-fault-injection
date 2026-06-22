/* ============================================================
   fault_injector.c -- external register-SEU on UNMODIFIED programs.

   Build flags:
     program: -DPROGRAM_QUICKSORT | -DPROGRAM_MATRIX | -DPROGRAM_EULER
     mode:    -DMODE_GOLDEN | -DMODE_FAULTY
     (faulty, optional) -DSEU_BIT=<0..31>
                        -DSEU_RANGE_LO_OFF=<hex>  -DSEU_RANGE_HI_OFF=<hex>

   WHAT CHANGED (and why it now actually lands):
   Disassembly of the real build showed two things:
     (1) prog_main is only ~0x162 bytes; a 4KB span was catching a dozen
         unrelated library functions, so injections hit random code.
     (2) The sort keeps its ARRAY BASE POINTER in r6 (a callee-saved register).
         r6 is NOT placed on the exception stack frame, so editing the stacked
         frame (r0-r3,r12) could never touch the data the sort actually uses.

   So this version:
     - gates on prog_main's REAL address range (symbol + tight offsets), and
     - corrupts the LIVE r6 register DIRECTLY inside a naked interrupt handler
       (r6 = the array base the running sort is using), then returns. Because
       r6 is callee-saved it is not on the frame; we must xor the physical
       register, which a naked handler can do.

   The program files are #included BYTE-FOR-BYTE; only their main() is renamed
   so the harness can call them. The fault comes entirely from the timer
   interrupt -- no injection code is added to the programs.

   Dual-core: the unmodified program runs on core 1 (so the timer IRQ and the
   stacked frame belong to the program); core 0 prints gate diagnostics without
   disturbing sampling. HardFault recovery catches a fatal corruption (e.g. r6
   driven out of valid RAM) as CRASH so the chip survives.
   ============================================================ */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

/* HardFault recovery: a fatal SEU longjmps back to be reported as CRASH. */
static jmp_buf g_fault_jmp;
static volatile bool g_in_run = false;
void __attribute__((used)) isr_hardfault(void){
    if(g_in_run) longjmp(g_fault_jmp, 1);
    while(1){}
}

/* ----- pick the program, rename its main, set its prog_main address ----- */
#if defined(PROGRAM_QUICKSORT)
  #define main prog_main
  #include "quicksort.c"
  #undef main
  #define PROG_NAME "quicksort"
#elif defined(PROGRAM_MATRIX)
  #define main prog_main
  #include "matrix_mult.c"
  #undef main
  #define PROG_NAME "matrix_mult"
#elif defined(PROGRAM_EULER)
  #define main prog_main
  #include "euler.c"
  #undef main
  #define PROG_NAME "euler"
#else
  #error "Define a PROGRAM_*"
#endif

#if !defined(MODE_GOLDEN) && !defined(MODE_FAULTY)
  #error "Define MODE_GOLDEN or MODE_FAULTY"
#endif

extern int prog_main(void);
#define TARGET_FN ((uintptr_t)&prog_main)

#ifdef MODE_FAULTY

/* Which bit of r6 to flip. */
#ifndef SEU_BIT
#define SEU_BIT 4
#endif

/* Address window (offsets from prog_main) in which to inject. For quicksort the
   inlined partition that uses r6 as the array base runs at prog_main+0x88 ..
   prog_main+0x110; we use a slightly wider window that stays inside prog_main
   so the fault lands while r6 is the live array base. These offsets come from
   the build's disassembly and are overridable per program. */
#ifndef SEU_RANGE_LO_OFF
#define SEU_RANGE_LO_OFF 0x2c
#endif
#ifndef SEU_RANGE_HI_OFF
#define SEU_RANGE_HI_OFF 0x162
#endif

/* Sampling period. Long enough that the handler (a short C call) always
   finishes and the re-armed alarm lands in the future; short enough to catch
   prog_main's brief active execution across the program's repeated passes. */
#ifndef SEU_PERIOD_US
#define SEU_PERIOD_US 5
#endif

static volatile int      g_bit = SEU_BIT;
static volatile int      g_alarm = 0;
static volatile bool     g_injected = false;
static volatile bool     g_just_injected = false;
static volatile uint32_t g_fire_count = 0;
static volatile uint32_t g_inrange_count = 0;
static volatile uint32_t g_seen_pc = 0;

/* Decision + bookkeeping in C (memory access is awkward in raw asm). Receives
   the stacked PC; returns the XOR mask to apply to live r6 (0 = do not inject).
   Must NOT be inlined -- the naked handler calls it by name. */
uint32_t __attribute__((used,noinline)) seu_decide(uint32_t pc){
    timer_hw->intr = (1u << g_alarm);          /* clear the alarm IRQ */
    g_fire_count++;
    g_seen_pc = pc;
    if(g_injected) return 0;
    uintptr_t lo = (TARGET_FN & ~1u) + SEU_RANGE_LO_OFF;
    uintptr_t hi = (TARGET_FN & ~1u) + SEU_RANGE_HI_OFF;
    if(pc >= lo && pc < hi){
        g_inrange_count++;
        g_injected = true;
        g_just_injected = true;
        irq_set_enabled(TIMER_IRQ_0, false);   /* one shot: stop sampling */
        return (1u << g_bit);                  /* corrupt live r6 by this mask */
    }
    timer_hw->alarm[g_alarm] = timer_hw->timerawl + SEU_PERIOD_US;  /* re-arm */
    return 0;
}

/* Naked handler: read the stacked PC, ask seu_decide for a mask, and if nonzero
   XOR it into the LIVE r6 (the running sort's array base) before exception
   return. r6 is callee-saved and not on the frame, so we edit the physical
   register here. r0-r3 are caller-saved/stacked (free to use); we save/restore
   r4 and the EXC_RETURN across the C call and keep the stack 8-byte aligned. */
__attribute__((naked)) void timer_inject_irq(void){
    __asm volatile(
        ".syntax unified     \n"
        "mrs  r0, msp        \n"   /* r0 = exception frame base            */
        "ldr  r0, [r0, #24]  \n"   /* r0 = stacked PC (frame word 6)       */
        "push {r4, lr}       \n"   /* save r4 (live) + EXC_RETURN, align 8  */
        "bl   seu_decide     \n"   /* r0 = mask (0 => no inject)            */
        "movs r2, r0         \n"   /* r2 = mask                            */
        "pop  {r4}           \n"   /* restore live r4                      */
        "pop  {r3}           \n"   /* r3 = EXC_RETURN                      */
        "cmp  r2, #0         \n"
        "beq  1f             \n"
        "eors r6, r6, r2     \n"   /* corrupt the LIVE array-base register  */
        "1:                  \n"
        "bx   r3             \n"   /* exception return                     */
    );
}

static void arm_inject_repeating(void){
    hw_set_bits(&timer_hw->inte, 1u << g_alarm);
    irq_set_exclusive_handler(TIMER_IRQ_0, timer_inject_irq);
    irq_set_enabled(TIMER_IRQ_0, true);
    timer_hw->alarm[g_alarm] = timer_hw->timerawl + SEU_PERIOD_US;
}

/* core 1: own the timer IRQ (same core the program runs on) and run the program */
void core1_main(void){
    arm_inject_repeating();
    prog_main();
    while(true){ tight_loop_contents(); }
}
#endif /* MODE_FAULTY */

int main(void){
    stdio_init_all();
    sleep_ms(300);

#ifdef MODE_GOLDEN
    printf("\nTESTBENCH_BEGIN %s GOLDEN\n", PROG_NAME);
    printf("--- GOLDEN ---\n");
    prog_main();
#else
    printf("\nTESTBENCH_BEGIN %s FAULTY r6 bit=%d\n", PROG_NAME, (int)SEU_BIT);
    printf("--- FAULTY r6 bit=%d (live array-base register, PC-gated) ---\n",
           (int)SEU_BIT);
    g_in_run = true;
    if(setjmp(g_fault_jmp)){
        g_in_run = false;
        printf("\nSEU_RESULT: CRASH (fatal corruption, recovered)\n");
        printf("\nTESTBENCH_DONE %s\n", PROG_NAME);
        while(true){ sleep_ms(1000); }
    }
    /* program runs on core 1 with the gate sampling it; core 0 reports status */
    multicore_launch_core1(core1_main);
    for(int i=0;i<8;i++){
        sleep_ms(1500);
        printf("GATE fires=%lu inrange=%lu injected=%d lastPC=0x%08lx win=0x%08lx-0x%08lx\n",
               (unsigned long)g_fire_count,(unsigned long)g_inrange_count,
               (int)g_injected,(unsigned long)g_seen_pc,
               (unsigned long)((TARGET_FN&~1u)+SEU_RANGE_LO_OFF),
               (unsigned long)((TARGET_FN&~1u)+SEU_RANGE_HI_OFF));
        if(g_just_injected){ g_just_injected=false;
            printf("SEU_INJECTED r6 bit=%d at PC=0x%08lx\n",(int)SEU_BIT,(unsigned long)g_seen_pc); }
    }
#endif

    printf("\nTESTBENCH_DONE %s\n", PROG_NAME);
    while(true){ sleep_ms(1000); }
    return 0;
}
