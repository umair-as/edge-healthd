import { signal } from '@preact/signals';

// Theme preference: follow the OS by default; an explicit choice is remembered.
// public/theme-init.js applies the same rule before first paint (an external
// file, because the server's CSP forbids inline scripts).

export type ThemePreference = 'system' | 'light' | 'dark';

const STORAGE_KEY = 'theme';
const media = () => window.matchMedia('(prefers-color-scheme: dark)');

export const themePreference = signal<ThemePreference>(readPreference());

function readPreference(): ThemePreference {
  try {
    const v = localStorage.getItem(STORAGE_KEY);
    return v === 'light' || v === 'dark' ? v : 'system';
  } catch {
    return 'system';
  }
}

export function applyTheme(pref: ThemePreference = themePreference.value): void {
  const dark = pref === 'dark' || (pref === 'system' && media().matches);
  document.documentElement.classList.toggle('dark', dark);
}

export function setThemePreference(pref: ThemePreference): void {
  themePreference.value = pref;
  try {
    if (pref === 'system') localStorage.removeItem(STORAGE_KEY);
    else localStorage.setItem(STORAGE_KEY, pref);
  } catch {
    // Not persisted; still applied for this session.
  }
  applyTheme(pref);
}

export const NEXT_THEME: Record<ThemePreference, ThemePreference> = {
  system: 'light',
  light: 'dark',
  dark: 'system',
};

/** Re-apply when the OS theme changes while following the system. */
export function watchSystemTheme(): () => void {
  const mq = media();
  const onChange = () => {
    if (themePreference.value === 'system') applyTheme('system');
  };
  mq.addEventListener('change', onChange);
  return () => mq.removeEventListener('change', onChange);
}
