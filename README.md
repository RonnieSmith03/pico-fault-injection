# Register-Level Fault Injection on a Raspberry Pi Pico (No Debugger)

## Introduction

This project injects a register-level fault into a running program on a
Raspberry Pi Pico (RP2040, Cortex-M0+) without using a debugger. No GDB, no
SWD, no external probe.

I have three benchmark programs, a quicksort, a matrix multiply, and a Euler solver.
I flip a single bit in one of the CPU's registers while one of these programs is
running, and I watch what happens to the output. The programs themselves are
never changed. All of the fault injection logic lives in a separate file, and
a single script handles building everything, flashing the board, running each
program with and without the fault, and writing out a report comparing the two.

As a point of clarity for anyone reading this, the matrix multiply program is
the reliably working case. It catches the fault and shows a clearly
corrupted result every run. QuickSort and Euler are a work in progress. The
injection mechanism is the same for all three, but landing the fault on the
exact register each program uses for its result is program specific, and I have
that fully working for matrix while still tuning it for the other two. I still
think it is important to document my work even though some aspects might be
failing while others succeed. For more details about some program specs,
please see the limitations section at the bottom for the details.

## How it works

The three programs, `quicksort.c`, `matrix_mult.c`, and `euler.c`, are compiled
exactly as they are, unaltered. I keep all of my injection code in a
separate file called `fault_injector.c`. This file is the one that actually
has a `main()` function, and it includes each of the three programs and
renames their `main` function at compile time so it can call them. This way
the original programs are never touched. I believe it is necessary to 
approach it in this manner because it may open the door to more general testing
for more benchmark programs in the future, that however requires further testing
and more dynamic code.

For the faulty run, I set up a repeating hardware timer interrupt that keeps
sampling the program counter while the chip runs. Once I see that the program
counter has landed inside the actual code of the target program (I got the
real address range for this from reading the disassembly of the build), a
naked interrupt handler flips one bit in a live CPU register, specifically
`r6`, and then returns. I picked `r6` because in the compiled version of
quicksort, that is the register holding the base pointer to the array being
sorted. `r6` does not get automatically saved onto the stack when an
interrupt happens, so I have to edit the actual physical register inside the
handler in assembly to reach it. This is the part I was mentioning earlier

Doing this gives a register-level fault inside a program that was never
modified. Depending on which bit I flip and when, I have seen the array come
out truncated, matrix values come out wrong, and in some cases the fault is
bad enough that it trips a HardFault, which I catch and report as a crash so
the chip does not lock up and the test can keep going.

The program runs on the second core of the chip while the first core prints
out diagnostics. I did this so that printing status messages does not get in
the way of the timing of the injection.

## What's in this repo

- `quicksort.c`, `matrix_mult.c`, `euler.c` — the three benchmark programs,
  unchanged. Each one has its own `main()`.
- `fault_injector.c` — the fault injection harness. Has the `main()`, the
  timer interrupt that checks the program counter, the handler that edits the
  register, the HardFault recovery, and the diagnostics that print from the
  other core.
- `CMakeLists.txt` — builds a golden version and a faulty version of each
  program, six firmware files total.
- `run_all.sh` — the script that does everything in one command: build, flash,
  capture the output, write the report, and exit.
- `pico_sdk_import.cmake` — the standard file the Pico SDK needs to set up the
  build.

## Dependencies, with the exact versions I used

- Raspberry Pi Pico SDK: 2.2.0
- picotool: 2.2.0-a4
- arm-none-eabi-gcc toolchain: 14_2_Rel1
- CMake: 3.13 or newer
- Host machine: Windows 11 running WSL2 (Ubuntu)
- usbipd-win, to pass the USB device into WSL
- Zadig, to install a WinUSB driver on the Pico
- Board: Raspberry Pi Pico (RP2040)

I built everything inside WSL2, but I flash using the Windows version of
picotool, called from inside WSL. Flashing reboots the board, and that breaks
the USB connection WSL was using, so flashing from the Windows side is what
has this working reliably for me.

## Setting up Windows and WSL (one time)

This was a technical hurdle that needed to be addressed before implementing
the script and the fault injector file. The Pico needs to be reachable over USB from
both Windows and WSL, and because flashing reboots the board, it has to get
handed back and forth between the two. That requires usbipd-win and a driver
installed through Zadig.

1. Install the Pico SDK and the toolchain. I used the VS Code Pico extension
   for this, which installed the SDK (2.2.0), the arm-none-eabi-gcc toolchain
   (14_2_Rel1), and picotool (2.2.0-a4) under `~/.pico-sdk/`.

2. Install usbipd-win. In an admin PowerShell window:
   ```powershell
   winget install usbipd
   ```

3. Install the USB driver using Zadig. Download it from
   https://zadig.akeo.ie/. Put the Pico into BOOTSEL mode by holding the
   BOOTSEL button while plugging it in. In Zadig, select the Pico from the
   dropdown at the top, set the driver to **WinUSB**, and click Install
   Driver.

   ![Zadig set to install the WinUSB driver](docs/zadig.png)

   This is what lets usbipd actually grab the device and forward it into WSL.
   Without this step the autoflash never worked for me.

4. Find the bus ID for the Pico. In PowerShell:
   ```powershell
   usbipd list
   ```
   Mine showed up as **`1-1`**. **Check your own output and use whatever
   BUSID shows up for you** if it's different, by changing the `BUSID`
   variable at the top of `run_all.sh`.

5. **Double check the picotool path in `run_all.sh` matches your own Windows
   username.** This will not work until you change `<you>` to your actual
   Windows username:
   ```
   /mnt/c/Users/<you>/.pico-sdk/picotool/2.2.0-a4/picotool/picotool.exe
   ```

## Building it

```bash
mkdir -p build && cd build
cmake ..
make -j4
```

This builds six firmware files inside `build/`: `inject_quicksort_GOLDEN.uf2`,
`inject_quicksort_FAULTY.uf2`, and the matrix and euler versions of the same.

## Running it, one command

```bash
./run_all.sh
```

This flashes the golden and faulty build for each program one after another,
captures the output from each, writes everything to a file called
`testbench_report_<timestamp>.txt`, prints it to the screen, and exits on its
own. It also handles the back and forth between Windows and WSL for you,
detaching the USB device before each flash and attaching it back after.

## Running it by hand

If you want to flash one build and watch the output live instead of running
the whole script, you can do this:

```bash
# hand the board over to Windows so it can flash (run in PowerShell)
# (use YOUR busid from `usbipd list`, not necessarily 1-1)
usbipd detach --busid 1-1

# flash a build from WSL, using the Windows copy of picotool
# (replace <you> with your own Windows username, same as in run_all.sh)
PT=/mnt/c/Users/<you>/.pico-sdk/picotool/2.2.0-a4/picotool/picotool.exe
"$PT" reboot -f -u
"$PT" load -x "$(wslpath -w build/inject_quicksort_FAULTY.uf2)"

# give the board back to WSL so it can read the serial port (PowerShell)
# (same busid as above)
usbipd attach --wsl --busid 1-1

# watch the output
stdbuf -oL cat /dev/ttyACM0
```

You can swap in any of the six firmware names to run a different program or
the golden version instead. Hit Ctrl-C to stop watching.

If the board ever locks up and shows as "Unknown USB Device" when you run
`usbipd list`, unplug it, hold down BOOTSEL, and plug it back in. That forces
it into the ROM bootloader no matter what the firmware was doing.

## How to check the output is correct

These are the correct outputs, the ones you should see in a golden run:

- quicksort: `Sorted array: 1 3 4 5 7 8 9 10 12`
- matrix: `3 3 3` on one line, `6 6 6` on the next
- euler: the last line should read `0.100000  1.107626`

A faulty run worked correctly if the output is different from the golden
output after the fault is injected. The clearest example is the matrix coming
out as `3 19 3` / `6 38 6` instead of the correct `3 3 3` / `6 6 6` -- matrix is
the reliable case and corrupts every run. QuickSort can show a truncated or
wrong array (for example `3 4 5 7 8`), and either program can print
`SEU_RESULT: CRASH`, meaning the flip was bad enough to crash the program and
the chip caught it and kept running instead of locking up. As noted above,
matrix is the dependable demonstration; quicksort and euler are still being
tuned and may or may not visibly change on a given run.

There are also some lines in the output that start with `GATE` that show what
the injection mechanism is actually doing while it runs:
- `fires` is how many times the timer has sampled the program counter
- `inrange` is how many of those samples caught the chip actually inside the
  target program's code
- `injected` tells you whether the bit flip has happened yet
- `lastPC` compared against `win` shows you the last sampled program counter
  against the address range I'm targeting, which is how I know the fault
  landed where it was supposed to

I noticed that the same flip to `r6` affects the three programs differently.
Quicksort and matrix both show visible corruption, but euler usually comes out
unaffected, because euler does not use `r6` to hold its result. To me, that is
a good sign that this is a real hardware effect and not something faked,
because a faked result would probably look the same across all three
programs.

## Tuning it

These can be set with `-D` flags passed to CMake. `run_all.sh` sets reasonable
defaults already.

- `SEU_BIT` (default is 4) — which bit of `r6` gets flipped. The higher the
  bit, the bigger the shift in the pointer, and the more likely it is to crash
  instead of quietly corrupt something.
- `SEU_RANGE_LO_OFF` and `SEU_RANGE_HI_OFF` — the window I'm watching for,
  given as an offset from the start of `prog_main`. The defaults here are
  specific to quicksort and came from reading its disassembly. Matrix and
  euler are laid out differently in memory, so if I want to tune those I have
  to go pull their offsets the same way, using
  `arm-none-eabi-objdump -d build/inject_<prog>_FAULTY.elf`.

## limitations and current status

I flip a bit in the `r6` register. In the compiled matrix multiply, `r6` ends
up holding a pointer into the data the multiply is working on, so flipping it
reliably corrupts the result -- matrix is the case I have fully working. The
other two programs use that register differently. QuickSort holds its array
pointer there too, but the fault often lands during the print rather than the
sort, so what gets corrupted is the display of the array rather than the sort
itself. Euler does its math in doubles, and on this chip (which has no
floating-point hardware) those doubles are carried in pairs of integer
registers, so `r6` is involved only during the tight calculation loop and not
during startup where the fault was landing. In short: same mechanism for all
three, but matrix is the dependable one and quicksort and euler are still being
improved for further testing.

The part of the program I'm actually trying to hit is short and only runs
every so often, so the timer sometimes needs a few passes before it actually
catches it. That's why the programs are set up to loop, so the injector gets
more than one chance.

This is research code I built with the assistance of Claude AI to explore
in-line assembly in a separate .c file and use that file to inject faults into
those programs. This is not a finished design because to accomplish a true
SEU, I would like to implement some type of reset functionality, along with fixing 
those issues previously mentioned certain issues.

## about the other directories (testbench2, testbench3)

This repo is the full project with all three programs. I split the follow-up
work into separate directories so each one has a clear objective that I would
like to acheive  and this one stays as a working  baseline:

- **testbench2** scales the project down to ONLY the matrix multiply. Since
  matrix is the case that works reliably end to end, that smaller version is a
  clean, self-contained demonstration of the technique with nothing half-tuned
  in it. The point of scaling down is to have one directory that is unambiguous
  about what works, without the in-progress quicksort and euler targeting in the
  way.
- **testbench3** is where I keep working on landing the fault correctly for
  euler and quicksort, targeting the exact spot in each program where the
  register actually holds the running result.

Keeping them separate means this directory never gets less stable as I
experiment, and the progression of the work stays easy to follow.


