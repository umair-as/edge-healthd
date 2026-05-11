#!/usr/bin/env bash
# benchmark.sh — edge-healthd performance profiler
#
# Usage (from host; set REMOTE to your gateway's ssh alias or user@host):
#   ssh $REMOTE 'bash -s' < scripts/benchmark.sh
#   ssh $REMOTE 'bash -s' < scripts/benchmark.sh 2>&1 | tee benchmark-$(date +%Y%m%d).log
#
# Results saved to: /data/edge-healthd-benchmark/<timestamp>/
#
# Tools used (graceful skip if absent):
#   strace  — syscall summary + timestamped trace
#   perf    — hardware counters (IPC, cache misses, branch mispredictions)
#   /proc   — memory, FD, sched, I/O footprint

set -euo pipefail

# ── Config ────────────────────────────────────────────────────────────────────
COLLECT_INTERVAL=60          # must match collect_interval_sec in healthd.conf
PROFILE_WINDOW=130           # seconds — covers 2 full collection cycles
TRACE_WINDOW=75              # seconds — timestamped strace (1 full burst + sleep)
OUT_DIR="/data/edge-healthd-benchmark/$(date -u +%Y%m%dT%H%M%SZ)"
DAEMON="edge-healthd"

# ── Helpers ───────────────────────────────────────────────────────────────────
have() { command -v "$1" &>/dev/null; }
section() { echo; echo "════════════════════════════════════════"; echo "  $*"; echo "════════════════════════════════════════"; }
warn() { echo "[SKIP] $*"; }

# ── Locate daemon ─────────────────────────────────────────────────────────────
PID=$(pgrep -x "$DAEMON" || true)
if [[ -z "$PID" ]]; then
    echo "ERROR: $DAEMON is not running" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
echo "Output directory : $OUT_DIR"
echo "Daemon           : $DAEMON PID=$PID"
echo "Kernel           : $(uname -r)"
echo "Binary version   : $(strings "/proc/$PID/exe" | grep -m1 '^v0\.' || echo unknown)"
echo "Timestamp        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "collect_interval : ${COLLECT_INTERVAL}s  profile_window: ${PROFILE_WINDOW}s"

# Save metadata
{
    echo "pid=$PID"
    echo "kernel=$(uname -r)"
    echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "binary=$(strings "/proc/$PID/exe" | grep -m1 '^v0\.' || echo unknown)"
    echo "config=$(cat /etc/edge/healthd.conf 2>/dev/null | tr '\n' ' ')"
} > "$OUT_DIR/meta.txt"

# ── Step 1: strace -c (syscall summary) ───────────────────────────────────────
if have strace; then
    section "Step 1: strace -c (${PROFILE_WINDOW}s, ~2 cycles)"
    strace -c -p "$PID" 2>"$OUT_DIR/strace_summary.txt" &
    STRACE_PID=$!
    echo "strace PID=$STRACE_PID, sleeping ${PROFILE_WINDOW}s..."
    sleep "$PROFILE_WINDOW"
    kill "$STRACE_PID" 2>/dev/null || true
    wait "$STRACE_PID" 2>/dev/null || true
    echo "--- strace -c output ---"
    cat "$OUT_DIR/strace_summary.txt"
else
    warn "strace not found"
fi

# ── Step 2: strace -T -tt (timestamped trace for burst wall-clock) ────────────
if have strace; then
    section "Step 2: strace -T -tt (${TRACE_WINDOW}s timestamped trace)"
    echo "Waiting up to ${COLLECT_INTERVAL}s to align with next collection burst..."
    timeout "$((COLLECT_INTERVAL + TRACE_WINDOW))" \
        strace -T -tt -p "$PID" -o "$OUT_DIR/strace_trace.log" || true
    TRACE_LINES=$(wc -l < "$OUT_DIR/strace_trace.log" 2>/dev/null || echo 0)
    echo "Trace lines: $TRACE_LINES  →  $OUT_DIR/strace_trace.log"
else
    warn "strace not found"
fi

# ── Step 3: perf stat (hardware counters) ─────────────────────────────────────
if have perf; then
    section "Step 3: perf stat (${PROFILE_WINDOW}s)"
    perf stat \
        -e cycles,instructions,cache-references,cache-misses,\
branch-instructions,branch-misses,\
L1-dcache-loads,L1-dcache-load-misses,\
LLC-loads,LLC-load-misses,\
context-switches,cpu-migrations,page-faults \
        -p "$PID" \
        -o "$OUT_DIR/perf_stat.txt" \
        -- sleep "$PROFILE_WINDOW" 2>&1 || true
    echo "--- perf stat output ---"
    cat "$OUT_DIR/perf_stat.txt"
else
    warn "perf not found"
fi

# ── Step 4: /proc snapshot ────────────────────────────────────────────────────
section "Step 4: /proc stats"
{
    echo "=== /proc/$PID/status ===" && cat "/proc/$PID/status"
    echo "=== /proc/$PID/smaps_rollup ===" && cat "/proc/$PID/smaps_rollup"
    echo "=== /proc/$PID/sched ===" && cat "/proc/$PID/sched"
    echo "=== /proc/$PID/io ===" && cat "/proc/$PID/io"
    echo "=== /proc/$PID/fd count ===" && ls "/proc/$PID/fd/" | wc -l
} | tee "$OUT_DIR/proc_stats.txt"

# ── Step 5: trace analysis ────────────────────────────────────────────────────
if [[ -f "$OUT_DIR/strace_trace.log" && $(wc -l < "$OUT_DIR/strace_trace.log") -gt 10 ]]; then
    section "Step 5: Trace analysis"
    LOG="$OUT_DIR/strace_trace.log"

    echo "--- syscall counts (1 cycle) ---"
    for sc in fstat openat mmap munmap sendmsg recvmsg ppoll sendto getdents64 fstatfs; do
        printf "  %-20s %s\n" "$sc:" "$(grep -c "$sc" "$LOG" || echo 0)"
    done

    echo "--- total kernel time ---"
    grep -oP '<\K[0-9.]+(?=>)' "$LOG" \
        | awk '{sum+=$1} END {printf "  %.6f sec\n", sum}'

    echo "--- ppoll blocking time ---"
    grep 'ppoll' "$LOG" \
        | grep -oP '<\K[0-9.]+(?=>)' \
        | awk '{sum+=$1; n++} END {printf "  %d calls, %.6f sec total\n", n, sum}'

    echo "--- burst wall-clock ---"
    grep -v 'restart_syscall\|clock_nanosleep\|futex' "$LOG" | head -1 \
        | awk '{print "  start:", $1}'
    grep -v 'restart_syscall\|clock_nanosleep\|futex' "$LOG" | tail -1 \
        | awk '{print "  end:  ", $1}'

    echo "--- files opened ---"
    grep 'openat.*= [0-9]' "$LOG" \
        | sed 's/.*openat[^"]*"//' | sed 's/".*//' \
        | sort | uniq -c | sort -rn | head -20 \
        | sed 's/^/  /'

    echo "--- D-Bus connections (socket+connect per cycle) ---"
    printf "  socket calls:  %s\n" "$(grep -c '^[0-9:.]* socket' "$LOG" || echo 0)"
    printf "  connect calls: %s\n" "$(grep -c '^[0-9:.]* connect' "$LOG" || echo 0)"

    {
        echo "=== Syscall counts ==="
        for sc in fstat openat mmap munmap sendmsg recvmsg ppoll sendto getdents64 fstatfs; do
            printf "%-20s %s\n" "$sc:" "$(grep -c "$sc" "$LOG" || echo 0)"
        done
        echo "=== Total kernel time ==="
        grep -oP '<\K[0-9.]+(?=>)' "$LOG" | awk '{sum+=$1} END {printf "%.6f sec\n", sum}'
        echo "=== ppoll ==="
        grep 'ppoll' "$LOG" | grep -oP '<\K[0-9.]+(?=>)' \
            | awk '{sum+=$1; n++} END {printf "%d calls, %.6f sec\n", n, sum}'
    } > "$OUT_DIR/analysis.txt"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
section "Summary"
echo "Results written to: $OUT_DIR"
ls -lh "$OUT_DIR/"
echo
echo "Key files:"
echo "  strace_summary.txt  — syscall counts + kernel time (2 cycles)"
echo "  strace_trace.log    — timestamped trace (burst wall-clock)"
echo "  perf_stat.txt       — hardware counters (IPC, cache misses)"
echo "  proc_stats.txt      — memory, FD, sched, I/O footprint"
echo "  analysis.txt        — parsed summary"
echo "  meta.txt            — version, kernel, config snapshot"
