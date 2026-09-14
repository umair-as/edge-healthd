import type { Severity } from '../types/health';
import { SEVERITY_LABEL } from '../lib/severity';

// Literal class names so Tailwind's JIT picks them up.
export const SEVERITY_TEXT: Record<Severity, string> = {
  ok: 'text-ok',
  warn: 'text-warn',
  crit: 'text-crit',
  unknown: 'text-unknown',
  stale: 'text-stale',
  unavailable: 'text-unavailable',
};

interface SeverityMarkProps {
  severity: Severity;
  class?: string;
  /** Decorative when the severity is already spelled out next to it. */
  decorative?: boolean;
}

// Shape carries meaning, not only color: filled dot for health states, hollow
// ring for stale, struck-through ring for unavailable, dashed ring for unknown.
export function SeverityMark({ severity, class: className = 'h-3 w-3', decorative = false }: SeverityMarkProps) {
  const a11y = decorative
    ? { 'aria-hidden': true as const }
    : { role: 'img' as const, 'aria-label': SEVERITY_LABEL[severity] };

  return (
    <svg viewBox="0 0 16 16" class={`shrink-0 ${SEVERITY_TEXT[severity]} ${className}`} {...a11y}>
      {severity === 'unknown' ? (
        <circle cx="8" cy="8" r="6" fill="none" stroke="currentColor" stroke-width="2.5" stroke-dasharray="3 2.5" />
      ) : severity === 'stale' || severity === 'unavailable' ? (
        <>
          <circle cx="8" cy="8" r="5.75" fill="none" stroke="currentColor" stroke-width="2.5" />
          {severity === 'unavailable' && (
            <line x1="3" y1="13" x2="13" y2="3" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" />
          )}
        </>
      ) : (
        <circle cx="8" cy="8" r="7" fill="currentColor" />
      )}
    </svg>
  );
}
