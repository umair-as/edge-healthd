import type { ComponentChildren } from 'preact';

interface MeterProps {
  label: ComponentChildren;
  /** Accessible name when `label` isn't plain text. */
  name: string;
  percent: number;
  valueText: string;
  /** Set when the daemon flagged this reading (e.g. a `disk_used_high` reason). */
  flagged?: 'warn' | 'crit' | null;
  hint?: ComponentChildren;
}

// A usage bar. Color comes from the daemon's judgement (`flagged`), never
// from thresholds re-invented in the UI, so the page can't disagree with the
// snapshot it is showing.
export function Meter({ label, name, percent, valueText, flagged = null, hint }: MeterProps) {
  const pct = Math.min(100, Math.max(0, Number.isFinite(percent) ? percent : 0));
  const fill = flagged === 'crit' ? 'bg-crit' : flagged === 'warn' ? 'bg-warn' : 'bg-ink-muted';

  return (
    <div>
      <div class="mb-1 flex items-baseline justify-between gap-4 text-sm">
        <span class="min-w-0 truncate">{label}</span>
        <span
          class={`shrink-0 font-mono text-[0.8125rem] ${
            flagged === 'crit' ? 'font-semibold text-crit' : flagged === 'warn' ? 'font-semibold text-warn' : ''
          }`}
        >
          {valueText}
        </span>
      </div>
      <div
        role="meter"
        aria-label={name}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(pct)}
        aria-valuetext={valueText}
        class="h-1.5 overflow-hidden rounded-full bg-line"
      >
        <div class={`h-full rounded-full ${fill}`} style={{ width: `${pct}%` }} />
      </div>
      {hint && <p class="mt-1 text-xs text-ink-muted">{hint}</p>}
    </div>
  );
}
