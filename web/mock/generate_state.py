#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Mock state.json generator for edge-healthd-ui development.

Generates realistic health state data with configurable scenarios.
"""

import argparse
import json
import os
import random
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def timestamp_now() -> str:
    """Return ISO 8601 timestamp in UTC."""
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def generate_device() -> dict:
    """Generate device info."""
    return {
        "device_id": "edge-dev-001",
        "hostname": "edge-gateway-01",
        "platform": random.choice(["rpi5", "visionfive2", "imx93"]),
        "arch": random.choice(["aarch64", "riscv64"]),
        "os": {
            "distro": "Buildroot",
            "version": "2024.02",
            "build_id": "dev-20240115",
            "kernel": "6.1.70-edge"
        }
    }


def generate_boot(scenario: str) -> dict:
    """Generate boot status."""
    uptime = random.randint(3600, 604800)  # 1 hour to 1 week
    boot_ok = scenario != "critical"

    return {
        "boot_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
        "last_boot_at": "2024-01-15T08:00:00Z",
        "uptime": uptime,
        "boot_ok": boot_ok,
        "boot_fail_count": 0 if boot_ok else random.randint(1, 3),
        "last_reboot_reason": None if boot_ok else "watchdog"
    }


def generate_services(scenario: str) -> dict:
    """Generate services status."""
    services = [
        ("edge-healthd.service", "active", "ok"),
        ("edge-mqtt.service", "active", "ok"),
        ("edge-ota.service", "active", "ok"),
        ("chronyd.service", "active", "ok"),
        ("systemd-networkd.service", "active", "ok"),
    ]

    units = []
    overall = "ok"

    for name, state, severity in services:
        # Add some variance based on scenario
        if scenario == "degraded" and name == "edge-mqtt.service":
            state = "active"
            severity = "warn"
            overall = "warn"
        elif scenario == "critical" and name == "edge-ota.service":
            state = "failed"
            severity = "crit"
            overall = "crit"

        unit = {
            "name": name,
            "state": state,
            "severity": severity,
            "since": "2024-01-15T08:00:00Z",
            "restart_count": random.randint(0, 3) if severity != "ok" else 0,
            "result": "success" if state == "active" else "exit-code",
            "detail": None,
            "log_excerpt": []
        }

        if severity != "ok":
            unit["log_excerpt"] = [
                f"Jan 15 08:00:01 edge {name}: Warning: Connection timeout",
                f"Jan 15 08:00:02 edge {name}: Retrying connection...",
            ]

        units.append(unit)

    return {
        "overall": overall,
        "units": units
    }


def generate_resources(scenario: str) -> dict:
    """Generate resource metrics."""
    # CPU load
    load_base = 0.5 if scenario == "healthy" else (1.5 if scenario == "degraded" else 3.0)
    cpu = {
        "load1": round(load_base + random.uniform(-0.2, 0.5), 2),
        "load5": round(load_base + random.uniform(-0.1, 0.3), 2),
        "load15": round(load_base + random.uniform(0, 0.2), 2),
    }

    # Memory
    total_mb = 4096
    used_pct = 45 if scenario == "healthy" else (75 if scenario == "degraded" else 92)
    used_mb = int(total_mb * used_pct / 100)

    memory = {
        "mem_total_mb": total_mb,
        "mem_used_mb": used_mb,
        "swap_used_mb": 0 if scenario == "healthy" else random.randint(50, 200),
    }

    # Storage
    storage = [
        {
            "mount": "/",
            "fs": "ext4",
            "used_pct": 35 if scenario == "healthy" else (75 if scenario == "degraded" else 95),
            "avail_mb": 2048,
        },
        {
            "mount": "/data",
            "fs": "ext4",
            "used_pct": 20,
            "avail_mb": 8192,
        },
    ]

    # Thermal
    temp_base = 45 if scenario == "healthy" else (65 if scenario == "degraded" else 82)
    thermal = [
        {"sensor": "cpu_thermal", "temp_c": round(temp_base + random.uniform(-2, 5), 1)},
        {"sensor": "gpu_thermal", "temp_c": round(temp_base - 5 + random.uniform(-2, 5), 1)},
    ]

    # Network
    network = [
        {
            "ifname": "eth0",
            "link": "up",
            "rx_bytes": random.randint(1000000, 100000000),
            "tx_bytes": random.randint(500000, 50000000),
            "rx_packets": random.randint(10000, 1000000),
            "tx_packets": random.randint(5000, 500000),
            "rx_dropped": random.randint(0, 10) if scenario != "healthy" else 0,
            "tx_dropped": 0,
            "rx_err": random.randint(0, 5) if scenario == "critical" else 0,
            "tx_err": 0,
            "ip": "192.168.1.100",
            "carrier": True,
            "speed_mbps": 1000,
            "duplex": "full",
        },
        {
            "ifname": "wlan0",
            "link": "down" if scenario == "critical" else "up",
            "rx_bytes": random.randint(100000, 10000000),
            "tx_bytes": random.randint(50000, 5000000),
            "rx_packets": random.randint(1000, 100000),
            "tx_packets": random.randint(500, 50000),
            "rx_dropped": 0,
            "tx_dropped": 0,
            "rx_err": 0,
            "tx_err": 0,
            "ip": "192.168.1.101" if scenario != "critical" else None,
            "carrier": scenario != "critical",
            "speed_mbps": None,
            "duplex": None,
        },
    ]

    return {
        "sample_window_sec": 60,
        "cpu": cpu,
        "memory": memory,
        "storage": storage,
        "thermal": thermal,
        "network": network,
    }


def generate_time_sync(scenario: str) -> dict:
    """Generate time sync status."""
    ntp_state = "locked" if scenario != "critical" else "free_running"

    rtc_voltage_mv = (
        2950 if scenario == "healthy"
        else 2650 if scenario == "degraded"
        else 2200  # critical — low battery
    )

    return {
        "overall": "ok" if ntp_state == "locked" else "warn",
        "source": "ntp",
        "ntp": {
            "enabled": True,
            "state": ntp_state,
            "last_sync_at": timestamp_now() if ntp_state == "locked" else None,
        },
        "ptp": {
            "enabled": False,
            "interface": None,
            "offset_ns": None,
            "rms_ns": None,
            "state": None,
            "last_sync_at": None,
            "role": None,
        },
        "rtc": {
            "enabled": True,
            "hctosys": scenario != "critical",
            "voltage_mv": rtc_voltage_mv,
            "drift_sec": round(random.uniform(-2.0, 2.0), 1),
        },
    }


def generate_update(scenario: str) -> dict:
    """Generate update status."""
    return {
        "overall": "ok",
        "active_slot": "A",
        "last_update": {
            "id": "update-2024-01-10-001",
            "installed_at": "2024-01-10T12:00:00Z",
            "result": "success",
            "detail": None,
        },
    }


def generate_journal(scenario: str) -> dict:
    """Generate journal status."""
    if scenario == "healthy":
        return {"overall": "ok", "error_count": 0, "recent_errors": []}
    elif scenario == "degraded":
        return {
            "overall": "warn",
            "error_count": 1,
            "recent_errors": [
                "Apr 10 07:55:12 edge-gateway-01 systemd[1]: edge-mqtt.service: Watchdog timeout (limit 30s)",
            ],
        }
    else:  # critical
        return {
            "overall": "crit",
            "error_count": 4,
            "recent_errors": [
                "Apr 10 08:12:01 edge-gateway-01 kernel: mmc0: error -110 whilst initialising SD card",
                "Apr 10 08:11:58 edge-gateway-01 edge-ota[512]: fatal: bundle verification failed",
                "Apr 10 08:09:44 edge-gateway-01 NetworkManager[341]: device eth0: carrier lost",
                "Apr 10 08:09:41 edge-gateway-01 kernel: eth0: renamed from veth3a2b1c",
            ],
        }


def generate_summary(services: dict, resources: dict, time_sync: dict, journal: dict) -> dict:
    """Generate overall summary from components."""
    severities = [
        services["overall"],
        time_sync["overall"],
        journal["overall"],
    ]

    # Check resources
    if resources["memory"]["mem_used_mb"] / resources["memory"]["mem_total_mb"] > 0.9:
        severities.append("crit")
    elif resources["memory"]["mem_used_mb"] / resources["memory"]["mem_total_mb"] > 0.7:
        severities.append("warn")

    # Determine overall
    if "crit" in severities:
        overall = "crit"
    elif "warn" in severities:
        overall = "warn"
    else:
        overall = "ok"

    # Build reasons
    reasons = []
    if overall == "ok":
        reasons = ["all_ok"]
    else:
        if services["overall"] != "ok":
            reasons.append("service_degraded")
        if time_sync["overall"] != "ok":
            reasons.append("time_sync_issue")
        if resources["memory"]["mem_used_mb"] / resources["memory"]["mem_total_mb"] > 0.7:
            reasons.append("high_memory")
        if journal["overall"] != "ok":
            reasons.append("journal_errors")

    return {
        "severity": overall,
        "reasons": reasons,
        "notes": None,
    }


_cycle_counter = 0


def generate_state(scenario: str = "healthy") -> dict:
    """Generate complete health state."""
    global _cycle_counter
    _cycle_counter += 1

    device = generate_device()
    boot = generate_boot(scenario)
    services = generate_services(scenario)
    resources = generate_resources(scenario)
    time_sync = generate_time_sync(scenario)
    update = generate_update(scenario)
    journal = generate_journal(scenario)
    summary = generate_summary(services, resources, time_sync, journal)

    return {
        "schema": "edge.health.state",
        "schema_version": "1.0",
        "generated_at": timestamp_now(),
        "cycle": _cycle_counter,
        "device": device,
        "boot": boot,
        "services": services,
        "resources": resources,
        "time_sync": time_sync,
        "update": update,
        "journal": journal,
        "summary": summary,
    }


def main():
    parser = argparse.ArgumentParser(description="Generate mock edge health state")
    parser.add_argument("--output", "-o", default="/data/edge/health/state.json",
                        help="Output file path")
    parser.add_argument("--scenario", "-s", choices=["healthy", "degraded", "critical"],
                        default="healthy", help="Health scenario")
    parser.add_argument("--interval", "-i", type=int, default=0,
                        help="Regenerate interval in seconds (0 = one-shot)")
    parser.add_argument("--rotate", "-r", action="store_true",
                        help="Rotate through scenarios")
    args = parser.parse_args()

    scenarios = ["healthy", "degraded", "critical"]
    scenario_idx = 0

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Generating state to {args.output}", file=sys.stderr)

    while True:
        if args.rotate:
            scenario = scenarios[scenario_idx % len(scenarios)]
            scenario_idx += 1
        else:
            scenario = args.scenario

        state = generate_state(scenario)

        # Atomic write
        tmp_path = output_path.with_suffix(".tmp")
        with open(tmp_path, "w") as f:
            json.dump(state, f, indent=2)
        tmp_path.rename(output_path)

        print(f"Generated {scenario} state at {state['generated_at']}", file=sys.stderr)

        if args.interval <= 0:
            break

        time.sleep(args.interval)


if __name__ == "__main__":
    main()
