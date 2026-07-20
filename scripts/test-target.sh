#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# test-target.sh — edge-healthd functional test suite (v0.5.1+)
#
# Usage (from host; set REMOTE to your gateway's ssh alias or user@host):
#   ssh $REMOTE 'bash -s' < scripts/test-target.sh
#   ssh $REMOTE 'bash -s' < scripts/test-target.sh 2>&1 | tee test-$(date +%Y%m%d).log
#
# Sections:
#   1.  Service health
#   2.  Snapshot file (tmpfs, valid JSON, fresh)
#   3.  Schema and top-level fields
#   4.  Cycle counter
#   5.  Device probe (once-only stability)
#   6.  Boot status
#   7.  Time sync — NTP + RTC (v0.5.1)
#   8.  Services
#   9.  Resources (baseline)
#   10. D-Bus interface
#   11. Collection cadence (ProbeSchedule)
#   12. Journal
#   13. Summary
#   14. stress-ng — CPU/memory pressure → severity escalation
#   15. fio — disk pressure → storage avail_mb / used_pct response
#
# Exit code: 0 = all pass, 1 = one or more failures

set -uo pipefail

SNAPSHOT="/run/health/state.json"
DAEMON="edge-healthd"
DBUS_DEST="edge.health"
DBUS_OBJ="/edge/health/manager"
DBUS_IFACE="edge.health.Manager"

# ── Helpers ───────────────────────────────────────────────────────────────────
PASS=0
FAIL=0
SKIP=0

pass()    { echo "  [PASS] $*"; ((PASS++)); }
fail()    { echo "  [FAIL] $*"; ((FAIL++)); }
skip()    { echo "  [SKIP] $*"; ((SKIP++)); }
section() { echo; echo "── $* ──────────────────────────────────────────────────"; }
have()    { command -v "$1" &>/dev/null; }

snap()    { jq -r "$1" "$SNAPSHOT" 2>/dev/null; }
snap_int(){ jq    "$1" "$SNAPSHOT" 2>/dev/null; }

trigger_and_wait() {
    # TriggerSnapshot is rate-limited (trigger_min_interval_sec, default 5s). If a
    # prior section triggered recently this call returns "b false" and no cycle
    # runs, so retry with spacing past the rate-limit window until it takes.
    local i r
    for i in 1 2 3 4; do
        r=$(busctl call "$DBUS_DEST" "$DBUS_OBJ" "$DBUS_IFACE" TriggerSnapshot 2>/dev/null || echo)
        [[ "$r" =~ "b true" ]] && { sleep 3; return 0; }
        sleep 6
    done
    sleep 3
    return 1  # never accepted within the retry budget
}

# ── 1. Service health ─────────────────────────────────────────────────────────
section "1. Service health"

if systemctl is-active --quiet "$DAEMON"; then
    pass "systemd unit is active"
else
    fail "systemd unit is NOT active"
    journalctl -u "$DAEMON" -n 20 --no-pager
fi

STATUS_LINE=$(systemctl show "$DAEMON" --property=StatusText --value 2>/dev/null || echo "")
if echo "$STATUS_LINE" | grep -qE '^v[0-9]+\.[0-9]+'; then
    pass "STATUS line has version: $STATUS_LINE"
else
    fail "STATUS line missing version string (got: '$STATUS_LINE')"
fi

# ── 2. Snapshot file ──────────────────────────────────────────────────────────
section "2. Snapshot file"

if [[ ! -f "$SNAPSHOT" ]]; then
    fail "snapshot file missing: $SNAPSHOT — aborting"
    exit 1
fi
pass "snapshot file exists"

FS_TYPE=$(stat -f -c '%T' "$SNAPSHOT" 2>/dev/null || echo unknown)
[[ "$FS_TYPE" == "tmpfs" ]] && pass "snapshot is on tmpfs" || fail "snapshot NOT on tmpfs (fs=$FS_TYPE)"

jq empty "$SNAPSHOT" 2>/dev/null && pass "snapshot is valid JSON" || { fail "snapshot is NOT valid JSON"; exit 1; }

AGE_S=$(( $(date +%s) - $(date -r "$SNAPSHOT" +%s) ))
(( AGE_S < 120 )) && pass "snapshot is fresh (age: ${AGE_S}s)" || fail "snapshot is stale (age: ${AGE_S}s)"

# ── 3. Schema and top-level fields ────────────────────────────────────────────
section "3. Schema and top-level fields"

[[ "$(snap '.schema')"         == "edge.health.state" ]] && pass "schema name"    || fail "schema name wrong: $(snap '.schema')"
[[ "$(snap '.schema_version')" == "1.1"               ]] && pass "schema version" || fail "schema_version wrong: $(snap '.schema_version')"

for field in generated_at cycle device boot services resources time_sync update journal summary; do
    jq -e "has(\"$field\")" "$SNAPSHOT" &>/dev/null \
        && pass "field '$field' present" \
        || fail "field '$field' MISSING"
done

# ── 4. Cycle counter ──────────────────────────────────────────────────────────
section "4. Cycle counter"

CYCLE1=$(snap_int '.cycle')
[[ "$CYCLE1" =~ ^[0-9]+$ && "$CYCLE1" -ge 1 ]] \
    && pass "cycle is a positive integer: $CYCLE1" \
    || fail "cycle invalid: '$CYCLE1'"

if have busctl; then
    trigger_and_wait
    CYCLE2=$(snap_int '.cycle')
    (( CYCLE2 > CYCLE1 )) \
        && pass "cycle incremented after TriggerSnapshot: $CYCLE1 → $CYCLE2" \
        || fail "cycle did not increment (before=$CYCLE1 after=$CYCLE2)"
else
    skip "busctl unavailable — skipping cycle increment check"
fi

# ── 5. Device probe (once-only) ───────────────────────────────────────────────
section "5. Device probe (once-only)"

DEVICE_ID=$(snap '.device.device_id')
ARCH=$(snap '.device.arch')
PLATFORM=$(snap '.device.platform')

[[ -n "$DEVICE_ID" && "$DEVICE_ID" != "unknown" ]] && pass "device_id: $DEVICE_ID" || fail "device_id missing/unknown"
[[ -n "$ARCH"      && "$ARCH"      != "unknown" ]] && pass "arch: $ARCH"           || fail "arch missing/unknown"
[[ -n "$PLATFORM"  && "$PLATFORM"  != "unknown" ]] && pass "platform: $PLATFORM"   || fail "platform missing/unknown"

DT_SERIAL=$(cat /proc/device-tree/serial-number 2>/dev/null | tr -d '\0' || echo "")
if [[ -n "$DT_SERIAL" && "$DEVICE_ID" == "$DT_SERIAL" ]]; then
    pass "device_id matches /proc/device-tree/serial-number"
else
    skip "device_id source unverifiable on this board"
fi

if have busctl; then
    ID_BEFORE=$(snap '.device.device_id'); ARCH_BEFORE=$(snap '.device.arch')
    trigger_and_wait
    ID_AFTER=$(snap '.device.device_id'); ARCH_AFTER=$(snap '.device.arch')
    [[ "$ID_BEFORE" == "$ID_AFTER" && "$ARCH_BEFORE" == "$ARCH_AFTER" ]] \
        && pass "DeviceProbe data stable across cycles (once-only)" \
        || fail "DeviceProbe data changed between cycles!"
fi

# ── 6. Boot status ────────────────────────────────────────────────────────────
section "6. Boot status"

[[ "$(snap '.boot.boot_ok')" == "true" ]] && pass "boot_ok=true" || fail "boot_ok is not true"
UPTIME=$(snap_int '.boot.uptime')
[[ "$UPTIME" =~ ^[0-9]+$ && "$UPTIME" -gt 0 ]] && pass "uptime: ${UPTIME}s" || fail "uptime invalid: $UPTIME"
FAIL_COUNT=$(snap_int '.boot.boot_fail_count')
[[ "$FAIL_COUNT" == "0" ]] && pass "boot_fail_count: 0" || fail "boot_fail_count non-zero: $FAIL_COUNT"

# ── 7. Time sync — NTP + RTC ─────────────────────────────────────────────────
section "7. Time sync (NTP + RTC)"

TS_OVERALL=$(snap '.time_sync.overall')
[[ "$TS_OVERALL" =~ ^(ok|warn)$ ]] && pass "time_sync.overall: $TS_OVERALL" || fail "time_sync.overall unexpected: $TS_OVERALL"

[[ "$(snap '.time_sync.ntp.enabled')" == "true" ]] && pass "ntp.enabled=true" || fail "ntp.enabled expected true"
NTP_STATE=$(snap '.time_sync.ntp.state // "null"')
[[ "$NTP_STATE" != "null" ]] && pass "ntp.state: $NTP_STATE" || fail "ntp.state null"

if jq -e '.time_sync | has("rtc")' "$SNAPSHOT" &>/dev/null; then
    pass "time_sync.rtc section present"
    RTC_ENABLED=$(snap '.time_sync.rtc.enabled')
    [[ "$RTC_ENABLED" == "true" ]] && pass "rtc.enabled=true" || fail "rtc.enabled=false"
    HCTOSYS=$(snap '.time_sync.rtc.hctosys')
    [[ "$HCTOSYS" =~ ^(true|false)$ ]] && pass "rtc.hctosys: $HCTOSYS" || fail "rtc.hctosys invalid"
    VOLT=$(snap_int '.time_sync.rtc.voltage_mv // 0')
    if (( VOLT > 0 )); then
        pass "rtc.voltage_mv: ${VOLT}mV"
        (( VOLT >= 2700 )) && pass "RTC battery OK (${VOLT}mV ≥ 2700mV)" || fail "RTC battery LOW: ${VOLT}mV"
    else
        skip "rtc.voltage_mv not available on this RTC"
    fi
else
    fail "time_sync.rtc MISSING — old binary still running?"
fi

PTP_ENABLED=$(snap '.time_sync.ptp.enabled')
[[ "$PTP_ENABLED" == "false" ]] && pass "ptp.enabled=false (default config)" || skip "ptp.enabled=$PTP_ENABLED (non-default)"

# ── 8. Services ───────────────────────────────────────────────────────────────
section "8. Services"

SVC_OVERALL=$(snap '.services.overall')
[[ "$SVC_OVERALL" =~ ^(ok|warn|crit|unknown)$ ]] && pass "services.overall: $SVC_OVERALL" || fail "invalid: $SVC_OVERALL"
SVC_COUNT=$(snap_int '.services.units | length')
(( SVC_COUNT >= 1 )) && pass "service units: $SVC_COUNT" || fail "no service units in snapshot"
BAD=$(jq '[.services.units[] | select((has("name") and has("state") and has("severity")) | not)] | length' "$SNAPSHOT")
[[ "$BAD" == "0" ]] && pass "all units have name/state/severity" || fail "$BAD unit(s) missing required fields"

# ── 9. Resources (baseline) ───────────────────────────────────────────────────
section "9. Resources (baseline)"

LOAD1=$(snap '.resources.cpu.load1')
MEM_TOTAL=$(snap_int '.resources.memory.mem_total_mb')
NET_COUNT=$(snap_int '.resources.network | length')
STOR_COUNT=$(snap_int '.resources.storage | length')

[[ "$LOAD1" =~ ^[0-9] ]]                             && pass "cpu.load1: $LOAD1"       || fail "cpu.load1 invalid"
(( MEM_TOTAL > 0 ))                                   && pass "mem_total_mb: $MEM_TOTAL" || fail "mem_total_mb invalid"
(( NET_COUNT >= 1 ))                                  && pass "network interfaces: $NET_COUNT" || fail "no interfaces"
(( STOR_COUNT >= 1 ))                                 && pass "storage mounts: $STOR_COUNT"    || fail "no storage mounts"
BAD_NIC=$(jq '[.resources.network[] | select((has("ifname") and has("link")) | not)] | length' "$SNAPSHOT")
[[ "$BAD_NIC" == "0" ]] && pass "all interfaces have ifname+link" || fail "$BAD_NIC interface(s) missing fields"

# ── 10. D-Bus interface ───────────────────────────────────────────────────────
section "10. D-Bus interface"

if have busctl; then
    busctl status "$DBUS_DEST" &>/dev/null \
        && pass "D-Bus service $DBUS_DEST registered" \
        || fail "D-Bus service $DBUS_DEST not found"

    SEV=$(busctl get-property "$DBUS_DEST" "$DBUS_OBJ" "$DBUS_IFACE" OverallSeverity 2>/dev/null | awk '{print $2}' | tr -d '"')
    [[ "$SEV" =~ ^(ok|warn|crit|unknown|stale|unavailable)$ ]] && pass "OverallSeverity: $SEV" || fail "OverallSeverity invalid: '$SEV'"

    # Use the retry helper, not a bare call: TriggerSnapshot is rate-limited
    # (trigger_min_interval_sec, default 5s), so a recent trigger from an earlier
    # section makes a single call return "b false" — a false failure. The helper
    # spaces retries past the rate-limit window and returns non-zero only if the
    # call is never accepted.
    if trigger_and_wait; then
        pass "TriggerSnapshot accepted (b true)"
    else
        fail "TriggerSnapshot never returned true within the retry budget"
    fi

    LOGS=$(busctl call "$DBUS_DEST" "$DBUS_OBJ" "$DBUS_IFACE" GetRecentLogs u 10 2>/dev/null || echo "error")
    [[ "$LOGS" != "error" ]] && pass "GetRecentLogs(10) OK" || fail "GetRecentLogs failed"

    busctl introspect "$DBUS_DEST" "$DBUS_OBJ" "$DBUS_IFACE" 2>/dev/null | grep -q HealthAlarm \
        && pass "HealthAlarm signal introspectable" \
        || fail "HealthAlarm not found in introspection"
else
    skip "busctl unavailable — skipping D-Bus tests"
fi

# ── 11. Collection cadence ────────────────────────────────────────────────────
section "11. Collection cadence"

if have busctl; then
    C_BEFORE=$(snap_int '.cycle')
    trigger_and_wait
    C_AFTER=$(snap_int '.cycle')
    DELTA=$(( C_AFTER - C_BEFORE ))
    [[ "$DELTA" -eq 1 ]] \
        && pass "cycle delta=1 per triggered collection" \
        || fail "cycle delta unexpected: $DELTA (before=$C_BEFORE after=$C_AFTER)"
else
    skip "busctl unavailable — skipping cadence test"
fi

# ── 12. Journal ───────────────────────────────────────────────────────────────
section "12. Journal"

J_OVERALL=$(snap '.journal.overall')
J_COUNT=$(snap_int '.journal.error_count')
[[ "$J_OVERALL" =~ ^(ok|warn|crit|unknown)$ ]] && pass "journal.overall: $J_OVERALL" || fail "journal.overall invalid"
[[ "$J_COUNT"   =~ ^[0-9]+$                 ]] && pass "journal.error_count: $J_COUNT" || fail "error_count invalid"

# ── 13. Summary ───────────────────────────────────────────────────────────────
section "13. Summary"

S_SEV=$(snap '.summary.severity')
[[ "$S_SEV" =~ ^(ok|warn|crit|unknown|stale|unavailable)$ ]] && pass "summary.severity: $S_SEV" || fail "summary.severity invalid"
REASONS_TYPE=$(jq '.summary.reasons | type' "$SNAPSHOT")
[[ "$REASONS_TYPE" == '"array"' ]] \
    && pass "summary.reasons is array ($(snap_int '.summary.reasons|length') entries)" \
    || fail "summary.reasons is not an array"

# ── 14. stress-ng — CPU/memory pressure ──────────────────────────────────────
section "14. stress-ng — CPU/memory pressure → severity escalation"

if ! have stress-ng || ! have busctl; then
    skip "stress-ng or busctl not available"
else
    SEV_BEFORE=$(snap '.summary.severity')
    pass "baseline severity: $SEV_BEFORE"

    # Skip escalation/recovery assertions if baseline is already degraded —
    # an unrelated service failure (e.g. telegraf) would mask the result.
    if [[ "$SEV_BEFORE" != "ok" ]]; then
        skip "baseline is '$SEV_BEFORE' (not ok) — skipping escalation/recovery assertions; stress still runs to verify probe updates"
        SKIP_STRESS_ASSERT=1
    else
        SKIP_STRESS_ASSERT=0
    fi

    # 4 CPU workers + 1 VM worker at 70% RAM for 90s — covers 1 full collect cycle
    echo "  Starting stress-ng (90s, 4 CPU + 70% VM)..."
    stress-ng --cpu 4 --vm 1 --vm-bytes 70% --timeout 90s &
    STRESS_PID=$!

    # Wait for two triggered collections under load
    sleep 10; trigger_and_wait
    LOAD_MID=$(snap '.resources.cpu.load1')
    SEV_MID=$(snap '.summary.severity')
    pass "under load — cpu.load1=$LOAD_MID  severity=$SEV_MID"

    sleep 15; trigger_and_wait
    SEV_PEAK=$(snap '.summary.severity')
    MEM_USED=$(snap_int '.resources.memory.mem_used_mb')
    MEM_TOTAL_NOW=$(snap_int '.resources.memory.mem_total_mb')
    MEM_PCT=$(awk "BEGIN {printf \"%d\", ($MEM_USED/$MEM_TOTAL_NOW)*100}")
    pass "peak — mem_used=${MEM_USED}MB (${MEM_PCT}%)  severity=$SEV_PEAK"

    if (( SKIP_STRESS_ASSERT == 0 )); then
        [[ "$SEV_PEAK" =~ ^(warn|crit)$ ]] \
            && pass "severity escalated under load: $SEV_BEFORE → $SEV_PEAK" \
            || fail "severity did NOT escalate (stayed '$SEV_PEAK') — check cpu_load_warn/mem_used_warn thresholds"
    fi

    wait "$STRESS_PID" 2>/dev/null || true
    echo "  stress-ng done, waiting for recovery (2 cycles)..."
    sleep 10; trigger_and_wait
    sleep 10; trigger_and_wait

    SEV_AFTER=$(snap '.summary.severity')
    LOAD_AFTER=$(snap '.resources.cpu.load1')

    if (( SKIP_STRESS_ASSERT == 0 )); then
        [[ "$SEV_AFTER" == "ok" ]] \
            && pass "severity recovered to ok after stress (load1=$LOAD_AFTER)" \
            || fail "severity did not recover: still '$SEV_AFTER' (load1=$LOAD_AFTER)"
    else
        pass "post-stress load1=$LOAD_AFTER  severity=$SEV_AFTER (recovery skipped — baseline was degraded)"
    fi
fi

# ── 15. fio — disk pressure → storage avail_mb response ─────────────────────
section "15. fio — disk pressure → storage avail_mb response"

if ! have fio || ! have busctl; then
    skip "fio or busctl not available"
else
    # Pick the /data mount if monitored, fallback to /tmp
    FIO_DIR="/data/fio-test"
    MOUNT_KEY=$(jq -r '[.resources.storage[] | select(.mount == "/data")] | first | .mount // ""' "$SNAPSHOT")
    if [[ -z "$MOUNT_KEY" ]]; then
        FIO_DIR="/tmp/fio-test"
        MOUNT_KEY=$(jq -r '[.resources.storage[] | select(.mount == "/")] | first | .mount // "/"' "$SNAPSHOT")
    fi
    mkdir -p "$FIO_DIR"

    AVAIL_BEFORE=$(jq "[.resources.storage[] | select(.mount == \"$MOUNT_KEY\")] | first | .avail_mb // 0" "$SNAPSHOT")
    PCT_BEFORE=$(jq "[.resources.storage[] | select(.mount == \"$MOUNT_KEY\")] | first | .used_pct // 0" "$SNAPSHOT")
    pass "baseline [$MOUNT_KEY] avail_mb=$AVAIL_BEFORE used_pct=${PCT_BEFORE}%"

    # Write 10% of available space, min 512MB, max 4096MB — ensures a visible
    # delta even on large volumes like /data (80GB+)
    FIO_SIZE_MB=$(awk "BEGIN {s=int($AVAIL_BEFORE*0.10); if(s<512) s=512; if(s>4096) s=4096; print s}")
    echo "  Running fio ${FIO_SIZE_MB}MB sequential write to $FIO_DIR (10% of ${AVAIL_BEFORE}MB avail)..."
    fio --name=fill --filename="$FIO_DIR/testfile" \
        --rw=write --bs=1M --size="${FIO_SIZE_MB}M" \
        --ioengine=libaio --direct=1 --iodepth=4 \
        --output-format=terse --output="$FIO_DIR/fio.log" 2>/dev/null || true

    trigger_and_wait

    AVAIL_AFTER=$(jq "[.resources.storage[] | select(.mount == \"$MOUNT_KEY\")] | first | .avail_mb // 0" "$SNAPSHOT")
    PCT_AFTER=$(jq "[.resources.storage[] | select(.mount == \"$MOUNT_KEY\")] | first | .used_pct // 0" "$SNAPSHOT")
    pass "after fio [$MOUNT_KEY] avail_mb=$AVAIL_AFTER used_pct=${PCT_AFTER}%"

    # Expect at least 80% of written size to show as consumed (direct I/O, no cache)
    EXPECTED_DELTA=$(awk "BEGIN {printf \"%d\", $FIO_SIZE_MB * 0.80}")
    DELTA_MB=$(awk "BEGIN {printf \"%d\", $AVAIL_BEFORE - $AVAIL_AFTER}")
    if (( DELTA_MB >= EXPECTED_DELTA )); then
        pass "avail_mb dropped by ${DELTA_MB}MB (wrote ${FIO_SIZE_MB}MB) — storage probe updated correctly"
    else
        fail "avail_mb delta ${DELTA_MB}MB < expected ${EXPECTED_DELTA}MB — probe may not have updated"
    fi

    # Cleanup
    rm -rf "$FIO_DIR"
    trigger_and_wait
    AVAIL_CLEAN=$(jq "[.resources.storage[] | select(.mount == \"$MOUNT_KEY\")] | first | .avail_mb // 0" "$SNAPSHOT")
    pass "after cleanup avail_mb=$AVAIL_CLEAN"
fi

# ── Results ───────────────────────────────────────────────────────────────────
echo
echo "════════════════════════════════════════════════════════"
printf "  Results:  %d passed  %d failed  %d skipped\n" "$PASS" "$FAIL" "$SKIP"
echo "════════════════════════════════════════════════════════"

(( FAIL > 0 )) && { echo "  OVERALL: FAIL"; exit 1; } || { echo "  OVERALL: PASS"; exit 0; }
