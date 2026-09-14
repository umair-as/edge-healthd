import type { Domain, HealthState, SectionFreshness, Severity } from '../types/health';
import { formatDuration, formatTemperature, plural } from './format';
import { normalizeSeverity, rank } from './severity';

// Canonical order: the order the daemon rolls domains up in, and the fixed
// position of each tile on the annunciator (tiles never move, so a
// technician learns where to look).
export const DOMAINS: readonly Domain[] = ['boot', 'services', 'resources', 'time_sync', 'update', 'journal', 'crash'];

export const DOMAIN_LABEL: Record<Domain, string> = {
  boot: 'Boot',
  services: 'Services',
  resources: 'Resources',
  time_sync: 'Time sync',
  update: 'Update',
  journal: 'Journal',
  crash: 'Crash',
};

export function isDomain(value: string): value is Domain {
  return (DOMAINS as readonly string[]).includes(value);
}

export function domainSection(state: HealthState, domain: Domain): SectionFreshness | undefined {
  return state[domain];
}

/**
 * Severity for one domain. Prefer the daemon's own `summary.domains` (it
 * already folds in staleness); fall back to the section for older snapshots.
 */
export function domainSeverity(state: HealthState, domain: Domain): Severity {
  const reported = state.summary.domains?.[domain];
  if (reported !== undefined) return normalizeSeverity(reported);

  switch (domain) {
    case 'boot':
      return state.boot.boot_ok ? 'ok' : 'crit';
    case 'services':
    case 'time_sync':
    case 'update':
    case 'journal':
      return normalizeSeverity(state[domain].overall);
    case 'resources':
      return 'unknown';
    case 'crash':
      if (!state.crash) return 'unknown';
      return state.crash.present && !state.crash.acknowledged ? 'warn' : 'ok';
  }
}

export function domainSeverities(state: HealthState): Record<Domain, Severity> {
  const out = {} as Record<Domain, Severity>;
  for (const d of DOMAINS) out[d] = domainSeverity(state, d);
  return out;
}

/** Problems first (worst severity), then canonical order for ties. */
export function orderByAttention(severities: Record<Domain, Severity>): Domain[] {
  return [...DOMAINS].sort(
    (a, b) => rank(severities[b]) - rank(severities[a]) || DOMAINS.indexOf(a) - DOMAINS.indexOf(b),
  );
}

/** Domains whose severity got worse between two snapshots. */
export function escalatedDomains(
  previous: Record<Domain, Severity> | null,
  current: Record<Domain, Severity>,
): Set<Domain> {
  const out = new Set<Domain>();
  if (!previous) return out;
  for (const d of DOMAINS) {
    if (rank(current[d]) > rank(previous[d])) out.add(d);
  }
  return out;
}

/** A one-line, plain-language summary of a domain for its collapsed row. */
export function domainSummary(state: HealthState, domain: Domain): string {
  switch (domain) {
    case 'boot': {
      const { boot } = state;
      if (!boot.boot_ok) {
        const reason = boot.last_reboot_reason ? ` · last reboot: ${boot.last_reboot_reason}` : '';
        return `Boot failed ${plural(boot.boot_fail_count, 'time')}${reason}`;
      }
      return `Up ${formatDuration(boot.uptime)}`;
    }

    case 'services': {
      const units = state.services.units;
      if (units.length === 0) return 'No units monitored';
      const failing = units.filter((u) => rank(normalizeSeverity(u.severity)) > rank('ok'));
      if (failing.length === 0) return `All ${plural(units.length, 'unit')} healthy`;
      if (failing.length === 1) return `${failing[0].name} is ${failing[0].state}`;
      return `${failing.length} of ${units.length} units need attention`;
    }

    case 'resources': {
      const { cpu, memory, storage = [], thermal = [] } = state.resources;
      const parts = [`Load ${cpu.load1.toFixed(2)}`];
      if (memory.mem_total_mb > 0) {
        parts.push(`Memory ${Math.round((memory.mem_used_mb / memory.mem_total_mb) * 100)}%`);
      }
      const hottest = thermal
        .filter((t) => t.available !== false && typeof t.temp_c === 'number')
        .reduce<number | null>((max, t) => (max === null || t.temp_c! > max ? t.temp_c! : max), null);
      if (hottest !== null) parts.push(formatTemperature(hottest));
      const blind = storage.filter((m) => m.available === false).length + thermal.filter((t) => t.available === false).length;
      if (blind > 0) parts.push(`${plural(blind, 'reading')} unavailable`);
      return parts.join(' · ');
    }

    case 'time_sync': {
      const ts = state.time_sync;
      if (ts.source === 'none') return 'No time source';
      const state_ = ts.source === 'ptp' ? ts.ptp?.state : ts.ntp?.state;
      return `${ts.source.toUpperCase()} ${(state_ ?? 'unknown').replace('_', ' ')}`;
    }

    case 'update': {
      const { active_slot, last_update } = state.update;
      const slot = active_slot ? `Slot ${active_slot}` : 'No active slot reported';
      if (!last_update) return `${slot} · no update history`;
      const outcome = { success: 'succeeded', failed: 'failed', unknown: 'result unknown' }[last_update.result];
      return `${slot} · last update ${outcome ?? last_update.result}`;
    }

    case 'journal': {
      const n = state.journal.error_count;
      return n === 0 ? 'No errors in scan window' : `${plural(n, 'error')} in scan window`;
    }

    case 'crash': {
      const crash = state.crash;
      if (!crash) return 'Not reported';
      if (!crash.present) return 'No crash records';
      const records = plural(crash.artifact_count, 'pstore record');
      return crash.acknowledged ? `${records} · acknowledged` : `${records} · not acknowledged`;
    }
  }
}
