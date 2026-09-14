import type { HealthState } from '../types/health';
import { connection, type Connection } from '../state/store';
import { NEXT_THEME, setThemePreference, themePreference, type ThemePreference } from '../state/theme';

const CONNECTION: Record<Connection, { label: string; dot: string; title: string }> = {
  connecting: { label: 'Connecting', dot: 'bg-ink-faint', title: 'Contacting the gateway' },
  live: { label: 'Live', dot: 'bg-ok', title: 'Updates are pushed as soon as a snapshot is written' },
  polling: { label: 'Polling', dot: 'bg-ink-muted', title: 'Push updates unavailable; checking every 5 seconds' },
  unreachable: { label: 'Unreachable', dot: 'bg-crit', title: "The gateway's web server is not responding" },
};

export function TopBar({ state }: { state: HealthState | null }) {
  const c = CONNECTION[connection.value];

  return (
    <header class="border-b border-line bg-panel">
      <div class="mx-auto flex max-w-5xl items-center gap-4 px-4 py-3 md:px-6">
        <div class="min-w-0 flex-1">
          <p class="eyebrow">edge-healthd</p>
          <h1 class="truncate text-base font-semibold leading-tight">
            {state?.device.hostname ?? 'Gateway'}
            {state && (
              <span class="ml-2 font-mono text-xs font-normal text-ink-muted">
                {state.device.platform} · {state.device.arch}
              </span>
            )}
          </h1>
        </div>

        <p class="flex items-center gap-2 text-sm text-ink-muted" title={c.title} role="status">
          <span class={`h-2 w-2 rounded-full ${c.dot}`} aria-hidden="true" />
          {c.label}
        </p>

        <ThemeToggle />
      </div>
    </header>
  );
}

const THEME_LABEL: Record<ThemePreference, string> = { system: 'System', light: 'Light', dark: 'Dark' };

function ThemeToggle() {
  const pref = themePreference.value;
  const next = NEXT_THEME[pref];

  return (
    <button
      type="button"
      onClick={() => setThemePreference(next)}
      class="flex items-center gap-1.5 rounded-md border border-line px-2 py-1 text-xs text-ink-muted hover:bg-raised hover:text-ink"
      aria-label={`Theme: ${THEME_LABEL[pref]}. Switch to ${THEME_LABEL[next]}`}
    >
      <svg viewBox="0 0 16 16" class="h-3.5 w-3.5" aria-hidden="true">
        {pref === 'system' ? (
          <path d="M8 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13Zm0 1.5v10a5 5 0 0 1 0-10Z" fill="currentColor" />
        ) : pref === 'light' ? (
          <g fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round">
            <circle cx="8" cy="8" r="3" />
            <path d="M8 1v1.5M8 13.5V15M1 8h1.5M13.5 8H15M3 3l1 1M12 12l1 1M3 13l1-1M12 4l1-1" />
          </g>
        ) : (
          <path d="M13.5 10.2A6 6 0 0 1 5.8 2.5a6 6 0 1 0 7.7 7.7Z" fill="currentColor" />
        )}
      </svg>
      {THEME_LABEL[pref]}
    </button>
  );
}
