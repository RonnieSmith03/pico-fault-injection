#!/bin/bash
# run_all_linux.sh -- ONE command, for NATIVE LINUX (not WSL).
# For each unmodified program: flash GOLDEN, capture; flash FAULTY, capture.
# Write one report, exit. No usbipd, no Zadig, no Windows picotool -- the Pico
# is on a real USB port, so we just flash with the native picotool and read the
# serial port directly.
#
# Requirements:
#   - picotool installed and on your PATH (run: picotool version)
#   - the Pico SDK udev rule installed (99-pico.rules) so picotool works without
#     sudo; otherwise run this script with sudo.
#   - your user in the 'dialout' group so you can read /dev/ttyACM* without sudo.

PICOTOOL="picotool"            # native picotool on PATH; change if yours is elsewhere
SERIAL="/dev/ttyACM0"          # change if your board enumerates as ttyACM1, etc.
DIR="$HOME/testbench"
BUILD_DIR="$DIR/build"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG="$DIR/testbench_report_${TIMESTAMP}.txt"
CAP_SECONDS=14                 # several program passes so the gate catches the compute
SETTLE=3
BIT=4
PROGRAMS=("quicksort" "matrix" "euler")

flash_and_capture() {
    local uf2="$1"; local outfile="$2"
    # reboot any running firmware into BOOTSEL, then load the new image
    "$PICOTOOL" reboot -f -u 2>/dev/null; sleep 2
    "$PICOTOOL" load -x "$uf2" 2>/dev/null; sleep "$SETTLE"
    # wait for the serial port to come back after the board reboots into the app
    local i; for i in $(seq 1 20); do [ -e "$SERIAL" ] && break; sleep 0.5; done
    [ -e "$SERIAL" ] || { echo "(serial port $SERIAL did not appear)" >> "$outfile"; return 1; }
    timeout "$CAP_SECONDS" cat "$SERIAL" >> "$outfile" 2>/dev/null || true
}

# sanity check: is picotool available?
if ! command -v "$PICOTOOL" >/dev/null 2>&1; then
    echo "ERROR: '$PICOTOOL' not found on PATH."
    echo "Install picotool (e.g. 'sudo apt install picotool' or build from source),"
    echo "or edit the PICOTOOL variable at the top of this script to its full path."
    exit 1
fi

{
echo "=========================================="
echo " TESTBENCH -- external register SEU on UNMODIFIED programs (native Linux)"
echo " $(date)"
echo " Fault: XOR bit $BIT into the live r6 register (array-base pointer)"
echo " while the CPU executes inside the target program, via timer interrupt."
echo "=========================================="
} > "$LOG"

cd "$BUILD_DIR" || { echo "build dir missing -- run: mkdir -p build && cd build && cmake .. && make -j4"; exit 1; }
cmake -DSEU_BIT=$BIT .. >/dev/null 2>&1
make -j4 >/dev/null 2>&1

for prog in "${PROGRAMS[@]}"; do
    G="$BUILD_DIR/inject_${prog}_GOLDEN.uf2"
    F="$BUILD_DIR/inject_${prog}_FAULTY.uf2"
    echo "" >> "$LOG"
    echo "##########################################" >> "$LOG"
    echo "# PROGRAM: ${prog}" >> "$LOG"
    echo "##########################################" >> "$LOG"

    echo ">>> ${prog}: GOLDEN"
    echo "" >> "$LOG"; echo "===== GOLDEN =====" >> "$LOG"
    [ -f "$G" ] && flash_and_capture "$G" "$LOG" || echo "MISSING $G" >> "$LOG"

    echo ">>> ${prog}: FAULTY (live r6)"
    echo "" >> "$LOG"; echo "===== FAULTY (r6 bit$BIT) =====" >> "$LOG"
    [ -f "$F" ] && flash_and_capture "$F" "$LOG" || echo "MISSING $F" >> "$LOG"
done

echo ""
echo "Done. Report: $LOG"
echo ""; echo "----- REPORT -----"; cat "$LOG"
