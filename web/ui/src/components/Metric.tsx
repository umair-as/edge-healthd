import type { ComponentChildren } from 'preact';

interface MetricProps {
  label: string;
  value: string | number;
  unit?: string;
  icon?: ComponentChildren;
  subtext?: string;
}

export function Metric({ label, value, unit, icon, subtext }: MetricProps) {
  return (
    <div class="flex items-start gap-3">
      {icon && (
        <div class="text-gray-400 dark:text-gray-500 mt-0.5">
          {icon}
        </div>
      )}
      <div>
        <p class="text-xs text-gray-500 dark:text-gray-400 uppercase tracking-wide">{label}</p>
        <p class="text-xl font-semibold">
          {value}
          {unit && <span class="text-sm font-normal text-gray-500 dark:text-gray-400 ml-1">{unit}</span>}
        </p>
        {subtext && (
          <p class="text-xs text-gray-500 dark:text-gray-400 mt-0.5">{subtext}</p>
        )}
      </div>
    </div>
  );
}

// Compact metric for lists
export function MetricRow({ label, value, className = '' }: { label: string; value: string | number; className?: string }) {
  return (
    <div class={`flex justify-between items-center py-1.5 ${className}`}>
      <span class="text-sm text-gray-600 dark:text-gray-400">{label}</span>
      <span class="text-sm font-medium">{value}</span>
    </div>
  );
}
