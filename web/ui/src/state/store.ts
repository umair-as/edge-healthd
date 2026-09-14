import { computed, signal } from '@preact/signals';
import type { Domain, HealthState, Severity } from '../types/health';
import { SNAPSHOT_SILENCE_SECONDS, estimateDeviceNow } from '../lib/clock';
import { domainSeverities, escalatedDomains } from '../lib/domains';
import { normalizeSeverity } from '../lib/severity';
import { parseHealthState } from '../lib/validate';

export type Connection = 'connecting' | 'live' | 'polling' | 'unreachable';

export type FeedProblem =
  | { kind: 'no-snapshot' }
  | { kind: 'invalid'; message: string }
  | { kind: 'unreachable' };

// ---------------------------------------------------------------------------
// Core state
// ---------------------------------------------------------------------------

export const snapshot = signal<HealthState | null>(null);
/** Browser time (ms) at which this UI first saw the current generated_at. */
export const firstSeenAt = signal<number | null>(null);
/** True while the snapshot on screen came from localStorage, not the gateway. */
export const fromCache = signal(false);
export const connection = signal<Connection>('connecting');
export const problem = signal<FeedProblem | null>(null);
/** Domains that got worse with the latest snapshot (drives the first-out flash). */
export const escalated = signal<ReadonlySet<Domain>>(new Set());

/** 1 Hz browser clock, so relative times re-render without per-component timers. */
export const now = signal(Date.now());

// ---------------------------------------------------------------------------
// Derived state
// ---------------------------------------------------------------------------

export const severities = computed<Record<Domain, Severity> | null>(() =>
  snapshot.value ? domainSeverities(snapshot.value) : null,
);

export const overall = computed<Severity>(() => normalizeSeverity(snapshot.value?.summary.severity));

/** Seconds since new data arrived, measured on the browser clock only. */
export const silenceSeconds = computed<number | null>(() =>
  firstSeenAt.value === null ? null : Math.max(0, (now.value - firstSeenAt.value) / 1000),
);

export const isSilent = computed(() => (silenceSeconds.value ?? 0) >= SNAPSHOT_SILENCE_SECONDS);

/** Estimated device-clock "now", for ages of device timestamps. */
export const deviceNow = computed<number | null>(() => {
  const s = snapshot.value;
  if (!s || firstSeenAt.value === null) return null;
  return estimateDeviceNow(s.generated_at, firstSeenAt.value, now.value);
});

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

export type AcceptResult = 'accepted' | 'unchanged' | 'rejected';

/**
 * Validate and apply a snapshot. An invalid payload never replaces a good
 * snapshot on screen; it only raises a problem.
 */
export function acceptSnapshot(raw: unknown, source: 'network' | 'cache', seenAt = Date.now()): AcceptResult {
  const parsed = parseHealthState(raw);
  if (!parsed.ok) {
    if (source === 'network') problem.value = { kind: 'invalid', message: parsed.error };
    return 'rejected';
  }

  const next = parsed.state;
  const prev = snapshot.value;

  if (source === 'network') {
    problem.value = null;
    fromCache.value = false;
  }

  if (prev && prev.generated_at === next.generated_at && prev.cycle === next.cycle) {
    return 'unchanged';
  }

  escalated.value = prev ? escalatedDomains(domainSeverities(prev), domainSeverities(next)) : new Set();
  snapshot.value = next;
  firstSeenAt.value = seenAt;
  if (source === 'cache') fromCache.value = true;
  return 'accepted';
}

export function reportProblem(p: FeedProblem): void {
  problem.value = p;
}

export function setConnection(c: Connection): void {
  connection.value = c;
}

let clockTimer: ReturnType<typeof setInterval> | null = null;

export function startClock(): () => void {
  if (clockTimer === null) {
    clockTimer = setInterval(() => {
      now.value = Date.now();
    }, 1000);
  }
  return () => {
    if (clockTimer !== null) clearInterval(clockTimer);
    clockTimer = null;
  };
}

/** Test helper: return every signal to its initial value. */
export function resetStore(): void {
  snapshot.value = null;
  firstSeenAt.value = null;
  fromCache.value = false;
  connection.value = 'connecting';
  problem.value = null;
  escalated.value = new Set();
  now.value = Date.now();
}
