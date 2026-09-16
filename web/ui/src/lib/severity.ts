import type { Severity } from '../types/health';

// Roll-up ranking from edge.health.state v1.1 §4:
//   unknown < ok < stale < unavailable < warn < crit
// `unknown` (never observed) sits below ok so warm-up never alarms; stale and
// unavailable surface above ok but never outrank a real warn/crit.
const RANK: Record<Severity, number> = {
  unknown: 0,
  ok: 1,
  stale: 2,
  unavailable: 3,
  warn: 4,
  crit: 5,
};

export const SEVERITY_LABEL: Record<Severity, string> = {
  ok: 'OK',
  warn: 'Warning',
  crit: 'Critical',
  unknown: 'Unknown',
  stale: 'Stale',
  unavailable: 'Unavailable',
};

export function isSeverity(value: unknown): value is Severity {
  return typeof value === 'string' && Object.prototype.hasOwnProperty.call(RANK, value);
}

// Values from a newer daemon that this UI doesn't know are shown as unknown
// rather than crashing a lookup.
export function normalizeSeverity(value: unknown): Severity {
  return isSeverity(value) ? value : 'unknown';
}

export function rank(severity: Severity): number {
  return RANK[severity];
}

export function worstOf(severities: Iterable<Severity>): Severity {
  let worst: Severity = 'unknown';
  for (const s of severities) {
    if (RANK[s] > RANK[worst]) worst = s;
  }
  return worst;
}

// Health states say something about the element; visibility-loss states say
// we can't currently see it.
export function isVisibilityLoss(severity: Severity): boolean {
  return severity === 'stale' || severity === 'unavailable';
}

// Anything above ok deserves attention: a real fault or a blind spot.
export function needsAttention(severity: Severity): boolean {
  return RANK[severity] > RANK.ok;
}

export function isFault(severity: Severity): boolean {
  return severity === 'warn' || severity === 'crit';
}
