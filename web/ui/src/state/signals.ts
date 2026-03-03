import { signal, computed } from '@preact/signals';
import type { HealthState, Severity } from '../types/health';

// Core state
export const healthState = signal<HealthState | null>(null);
export const isConnected = signal<boolean>(false);
export const isOffline = signal<boolean>(!navigator.onLine);
export const isDarkMode = signal<boolean>(
  document.documentElement.classList.contains('dark')
);
export const lastUpdate = signal<Date | null>(null);

// Derived signals
export const overallSeverity = computed<Severity>(() => {
  return healthState.value?.summary.severity ?? 'unknown';
});

export const hostname = computed<string>(() => {
  return healthState.value?.device.hostname ?? 'Unknown Device';
});

export const uptimeFormatted = computed<string>(() => {
  const uptime = healthState.value?.boot.uptime;
  if (uptime === undefined) return '--';

  const days = Math.floor(uptime / 86400);
  const hours = Math.floor((uptime % 86400) / 3600);
  const minutes = Math.floor((uptime % 3600) / 60);

  if (days > 0) {
    return `${days}d ${hours}h`;
  } else if (hours > 0) {
    return `${hours}h ${minutes}m`;
  } else {
    return `${minutes}m`;
  }
});

export const memoryUsedPercent = computed<number>(() => {
  const mem = healthState.value?.resources.memory;
  if (!mem || mem.mem_total_mb === 0) return 0;
  return Math.round((mem.mem_used_mb / mem.mem_total_mb) * 100);
});

export const cpuLoadFormatted = computed<string>(() => {
  const cpu = healthState.value?.resources.cpu;
  if (!cpu) return '--';
  return cpu.load1.toFixed(2);
});

export const servicesOkCount = computed<number>(() => {
  const services = healthState.value?.services.units ?? [];
  return services.filter(s => s.severity === 'ok').length;
});

export const servicesTotalCount = computed<number>(() => {
  return healthState.value?.services.units.length ?? 0;
});

export const timeSinceUpdate = computed<string>(() => {
  const last = lastUpdate.value;
  if (!last) return 'Never';

  const seconds = Math.floor((Date.now() - last.getTime()) / 1000);
  if (seconds < 5) return 'Just now';
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  return `${Math.floor(seconds / 3600)}h ago`;
});

// Actions
export function updateHealthState(state: HealthState) {
  healthState.value = state;
  lastUpdate.value = new Date();
}

export function setConnected(connected: boolean) {
  isConnected.value = connected;
}

export function setOffline(offline: boolean) {
  isOffline.value = offline;
}

export function toggleDarkMode() {
  const newValue = !isDarkMode.value;
  isDarkMode.value = newValue;

  if (newValue) {
    document.documentElement.classList.add('dark');
    localStorage.setItem('theme', 'dark');
  } else {
    document.documentElement.classList.remove('dark');
    localStorage.setItem('theme', 'light');
  }
}
