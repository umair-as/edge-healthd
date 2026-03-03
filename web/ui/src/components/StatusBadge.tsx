import type { Severity } from '../types/health';

interface StatusBadgeProps {
  severity: Severity;
  size?: 'sm' | 'md' | 'lg';
  label?: string;
  pulse?: boolean;
}

const severityColors: Record<Severity, string> = {
  ok: 'bg-severity-ok',
  warn: 'bg-severity-warn',
  crit: 'bg-severity-crit',
  unknown: 'bg-severity-unknown',
};

const severityLabels: Record<Severity, string> = {
  ok: 'OK',
  warn: 'Warning',
  crit: 'Critical',
  unknown: 'Unknown',
};

const sizeClasses: Record<string, { dot: string; text: string }> = {
  sm: { dot: 'w-2 h-2', text: 'text-xs' },
  md: { dot: 'w-3 h-3', text: 'text-sm' },
  lg: { dot: 'w-4 h-4', text: 'text-base' },
};

export function StatusBadge({ severity, size = 'md', label, pulse = false }: StatusBadgeProps) {
  const { dot, text } = sizeClasses[size];
  const displayLabel = label ?? severityLabels[severity];

  return (
    <span class="inline-flex items-center gap-1.5">
      <span class="relative flex">
        <span class={`${dot} rounded-full ${severityColors[severity]}`} />
        {pulse && severity !== 'unknown' && (
          <span class={`absolute inset-0 ${dot} rounded-full ${severityColors[severity]} animate-ping opacity-75`} />
        )}
      </span>
      <span class={`${text} font-medium capitalize`}>{displayLabel}</span>
    </span>
  );
}

// Simple dot-only badge
export function StatusDot({ severity, pulse = false }: { severity: Severity; pulse?: boolean }) {
  return (
    <span class="relative flex">
      <span class={`w-2.5 h-2.5 rounded-full ${severityColors[severity]}`} />
      {pulse && severity !== 'unknown' && (
        <span class={`absolute inset-0 w-2.5 h-2.5 rounded-full ${severityColors[severity]} animate-ping opacity-75`} />
      )}
    </span>
  );
}
