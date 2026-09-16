import type { HealthState } from '../types/health';

// The last snapshot is cached so the page can show something (clearly marked
// as cached) when the gateway is unreachable. The cached payload is treated
// as untrusted and re-validated on load, like network data.

const STORAGE_KEY = 'edge-healthd-state';

interface Cached {
  state: HealthState;
  firstSeenAt: number;
}

export function persistSnapshot(state: HealthState, firstSeenAt: number): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({ state, firstSeenAt } satisfies Cached));
  } catch {
    // Storage full or disabled: caching is best-effort.
  }
}

export function loadCachedSnapshot(): { state: unknown; firstSeenAt: number } | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const parsed: unknown = JSON.parse(raw);
    if (typeof parsed !== 'object' || parsed === null) return null;
    const { state, firstSeenAt } = parsed as Partial<Cached>;
    if (state === undefined || typeof firstSeenAt !== 'number') return null;
    return { state, firstSeenAt };
  } catch {
    return null;
  }
}
