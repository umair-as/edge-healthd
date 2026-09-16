import type { HealthState } from '../types/health';
import { isFault, normalizeSeverity } from './severity';

// Reason codes are `<code>` or `<code>:<subject>` (schema §5). The UI uses
// them to point at *which* reading the daemon flagged, instead of applying
// its own thresholds.

export interface Reason {
  code: string;
  subject: string | null;
}

export function parseReason(raw: string): Reason {
  const i = raw.indexOf(':');
  return i === -1 ? { code: raw, subject: null } : { code: raw.slice(0, i), subject: raw.slice(i + 1) };
}

export function hasReason(state: HealthState, code: string, subject?: string): boolean {
  return state.summary.reasons.some((raw) => {
    const r = parseReason(raw);
    return r.code === code && (subject === undefined || r.subject === subject);
  });
}

/**
 * Flag level for a resource reading the daemon called out. Reason codes don't
 * carry a level, so use the resources domain's severity when it is a fault.
 */
export function resourceFlag(state: HealthState, code: string, subject?: string): 'warn' | 'crit' | null {
  if (!hasReason(state, code, subject)) return null;
  const sev = normalizeSeverity(state.summary.domains?.resources ?? 'warn');
  return isFault(sev) ? (sev as 'warn' | 'crit') : 'warn';
}
