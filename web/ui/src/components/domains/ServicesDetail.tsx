import type { ServiceUnit } from '../../types/health';
import { formatTimestamp, plural } from '../../lib/format';
import { needsAttention, normalizeSeverity, rank } from '../../lib/severity';
import { SeverityMark } from '../SeverityMark';

export function ServicesDetail({ units }: { units: ServiceUnit[] }) {
  if (units.length === 0) {
    return (
      <p class="text-sm text-ink-muted">
        No units are monitored. Add them to <code class="font-mono">monitored_services</code> in the daemon config.
      </p>
    );
  }

  const sorted = [...units].sort(
    (a, b) => rank(normalizeSeverity(b.severity)) - rank(normalizeSeverity(a.severity)) || a.name.localeCompare(b.name),
  );

  return (
    <ul class="divide-y divide-line">
      {sorted.map((unit) => (
        <UnitRow key={unit.name} unit={unit} />
      ))}
    </ul>
  );
}

function UnitRow({ unit }: { unit: ServiceUnit }) {
  const severity = normalizeSeverity(unit.severity);
  const attention = needsAttention(severity);
  const logs = unit.log_excerpt ?? [];

  return (
    <li class="py-2.5 first:pt-0 last:pb-0">
      <div class="flex flex-wrap items-center gap-x-3 gap-y-1">
        <SeverityMark severity={severity} />
        <span class="font-mono text-[0.8125rem]">{unit.name}</span>
        <span class={`text-sm ${attention ? 'font-medium' : 'text-ink-muted'}`}>{unit.state}</span>
        {unit.restart_count > 0 && (
          <span class="text-sm text-ink-muted">· {plural(unit.restart_count, 'restart')}</span>
        )}
        {unit.since && (
          <span class="ml-auto text-xs text-ink-faint">since {formatTimestamp(unit.since)}</span>
        )}
      </div>

      {attention && (unit.detail || unit.result) && (
        <p class="mt-1 pl-6 text-sm text-ink-muted">
          {unit.detail ?? `Result: ${unit.result}`}
        </p>
      )}

      {attention && logs.length > 0 && (
        <pre
          class="mt-2 ml-6 max-h-48 overflow-auto rounded-md bg-raised p-3 font-mono text-xs leading-relaxed text-ink-muted"
          role="region"
          aria-label={`Recent log lines for ${unit.name}`}
          tabIndex={0}
        >
          {logs.join('\n')}
        </pre>
      )}
    </li>
  );
}
