#!/bin/bash
# run_all.sh -- ONE command (Windows + WSL2). For each unmodified program: flash
# GOLDEN, capture; flash FAULTY (live-r6 register SEU), capture. Write one
# report, exit. Uses usbipd + the Windows picotool to bridge the board into WSL.

PT="/mnt/c/Users/Owner/.pico-sdk/picotool/2.2.0-a4/picotool/picotool.exe"
BUSID="1-1"
# resolve this script's own directory, so it works no matter where it's cloned
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$DIR/build"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG="$DIR/testbench_report_${TIMESTAMP}.txt"
CAP_SECONDS=14          # several program passes so the gate catches the compute
SETTLE=4
BIT=4
PROGRAMS=("quicksort" "matrix" "euler")

flash_and_capture() {
    local uf2="$1"; local outfile="$2"
    local win_uf2; win_uf2=$(wslpath -w "$uf2")
    usbipd.exe detach --busid "$BUSID" 2>/dev/null; sleep 2
    "$PT" reboot -f -u 2>/dev/null; sleep 2
    "$PT" load -x "$win_uf2" 2>/dev/null; sleep 3
    usbipd.exe attach --wsl --busid "$BUSID" 2>/dev/null; sleep "$SETTLE"
    local i; for i in $(seq 1 20); do [ -e /dev/ttyACM0 ] && break; sleep 0.5; done
    [ -e /dev/ttyACM0 ] || { echo "(port did not appear)" >> "$outfile"; return 1; }
    timeout "$CAP_SECONDS" cat /dev/ttyACM0 >> "$outfile" 2>/dev/null || true
}

{
echo "=========================================="
echo " TESTBENCH -- external register SEU on UNMODIFIED programs"
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
