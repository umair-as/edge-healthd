interface ProgressBarProps {
  value: number;
  max?: number;
  label?: string;
  showValue?: boolean;
  thresholds?: {
    warn: number;
    crit: number;
  };
  size?: 'sm' | 'md';
}

export function ProgressBar({
  value,
  max = 100,
  label,
  showValue = true,
  thresholds = { warn: 70, crit: 90 },
  size = 'md',
}: ProgressBarProps) {
  const percent = Math.min(Math.round((value / max) * 100), 100);

  let colorClass = 'bg-severity-ok';
  if (percent >= thresholds.crit) {
    colorClass = 'bg-severity-crit';
  } else if (percent >= thresholds.warn) {
    colorClass = 'bg-severity-warn';
  }

  const heightClass = size === 'sm' ? 'h-1.5' : 'h-2.5';

  return (
    <div class="w-full">
      {(label || showValue) && (
        <div class="flex justify-between items-center mb-1">
          {label && <span class="text-sm text-gray-600 dark:text-gray-400">{label}</span>}
          {showValue && <span class="text-sm font-medium">{percent}%</span>}
        </div>
      )}
      <div class={`w-full ${heightClass} bg-gray-200 dark:bg-slate-700 rounded-full overflow-hidden`}>
        <div
          class={`${heightClass} ${colorClass} rounded-full transition-all duration-300`}
          style={{ width: `${percent}%` }}
        />
      </div>
    </div>
  );
}
