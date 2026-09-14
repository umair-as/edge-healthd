import type { Severity } from '../types/health';

interface StatusBadgeProps {
  severity: Severity;
  size?: 'sm' | 'md' | 'lg';
  label?: string;
  pulse?: boolean;
}

// Literal class names so Tailwind's JIT picks them up.
const severityText: Record<Severity, string> = {
  ok: 'text-severity-ok',
  warn: 'text-severity-warn',
  crit: 'text-severity-crit',
  unknown: 'text-severity-unknown',
  stale: 'text-severity-stale',
  unavailable: 'text-severity-unavailable',
};

const severityBg: Record<Severity, string> = {
  ok: 'bg-severity-ok',
  warn: 'bg-severity-warn',
  crit: 'bg-severity-crit',
  unknown: 'bg-severity-unknown',
  stale: 'bg-severity-stale',
  unavailable: 'bg-severity-unavailable',
};

const severityLabels: Record<Severity, string> = {
  ok: 'OK',
  warn: 'Warning',
  crit: 'Critical',
  unknown: 'Unknown',
  stale: 'Stale',
  unavailable: 'Unavailable',
};

const sizeClasses: Record<string, { dot: string; text: string }> = {
  sm: { dot: 'w-2 h-2', text: 'text-xs' },
  md: { dot: 'w-3 h-3', text: 'text-sm' },
  lg: { dot: 'w-4 h-4', text: 'text-base' },
};

// Loss-of-observability states: we can't see the element, which is not the same
// as it being healthy or unhealthy.
function isVisibilityLoss(severity: Severity): boolean {
  return severity === 'stale' || severity === 'unavailable';
}

// Health states render as a filled dot; stale as a hollow ring; unavailable as a
// struck-through ring — distinguishable by shape, not only by color.
function SeverityMark({ severity, dot, pulse }: { severity: Severity; dot: string; pulse: boolean }) {
  const shouldPulse = pulse && severity !== 'unknown' && !isVisibilityLoss(severity);

  return (
    <span class="relative flex" role="img" aria-label={severityLabels[severity]}>
      <svg class={`${dot} ${severityText[severity]}`} viewBox="0 0 16 16" aria-hidden="true">
        {isVisibilityLoss(severity) ? (
          <>
            <circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" stroke-width="3" />
            {severity === 'unavailable' && (
              <line x1="2.5" y1="13.5" x2="13.5" y2="2.5" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" />
            )}
          </>
        ) : (
          <circle cx="8" cy="8" r="8" fill="currentColor" />
        )}
      </svg>
      {shouldPulse && (
        <span class={`absolute inset-0 ${dot} rounded-full ${severityBg[severity]} animate-ping opacity-75`} />
      )}
    </span>
  );
}

export function StatusBadge({ severity, size = 'md', label, pulse = false }: StatusBadgeProps) {
  const { dot, text } = sizeClasses[size];
  const displayLabel = label ?? severityLabels[severity] ?? severity;

  return (
    <span class="inline-flex items-center gap-1.5">
      <SeverityMark severity={severity} dot={dot} pulse={pulse} />
      <span class={`${text} font-medium capitalize`}>{displayLabel}</span>
    </span>
  );
}

// Simple dot-only badge
export function StatusDot({ severity, pulse = false }: { severity: Severity; pulse?: boolean }) {
  return <SeverityMark severity={severity} dot="w-2.5 h-2.5" pulse={pulse} />;
}
