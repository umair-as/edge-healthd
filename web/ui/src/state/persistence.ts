import type { HealthState } from '../types/health';

const STORAGE_KEY = 'edge-healthd-state';

// Save state to localStorage for offline viewing
export function persistState(state: HealthState): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  } catch {
    // Storage might be full or disabled, silently fail
  }
}

// Load persisted state from localStorage
export function loadPersistedState(): HealthState | null {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (stored) {
      return JSON.parse(stored) as HealthState;
    }
  } catch {
    // Invalid JSON or storage error
  }
  return null;
}

// Clear persisted state
export function clearPersistedState(): void {
  try {
    localStorage.removeItem(STORAGE_KEY);
  } catch {
    // Silently fail
  }
}
