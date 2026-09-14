import type { Domain, Severity } from '../types/health';
import { DOMAINS, DOMAIN_LABEL } from '../lib/domains';
import { SEVERITY_LABEL } from '../lib/severity';

// An annunciator panel: one fixed tile per domain. "Dark cockpit" convention —
// healthy tiles stay unlit, so the eye goes straight to whatever is lit.
// Faults light up in their color; loss of visibility is hatched rather than
// colored, because "can't see it" is not a point on the health scale.

const TILE: Record<Severity, string> = {
  ok: 'border-line bg-panel text-ink-muted',
  unknown: 'border-dashed border-line bg-panel text-ink-faint',
  warn: 'border-warn bg-warn/[0.14] text-warn',
  crit: 'border-crit bg-crit/[0.14] text-crit',
  stale: 'border-dashed border-stale hatch-stale text-stale',
  unavailable: 'border-dashed border-unavailable hatch-unavailable text-unavailable',
};

const STATE_WORD: Record<Severity, string> = { ...SEVERITY_LABEL, unknown: 'Not observed' };

interface AnnunciatorProps {
  severities: Record<Domain, Severity>;
  escalated: ReadonlySet<Domain>;
  onSelect: (domain: Domain) => void;
}

export function Annunciator({ severities, escalated, onSelect }: AnnunciatorProps) {
  return (
    <nav aria-label="Domains">
      <ul class="grid grid-cols-2 gap-2 sm:grid-cols-4 lg:grid-cols-7">
        {DOMAINS.map((domain) => {
          const severity = severities[domain];
          const lit = severity !== 'ok' && severity !== 'unknown';
          return (
            <li key={domain}>
              <button
                type="button"
                onClick={() => onSelect(domain)}
                aria-label={`${DOMAIN_LABEL[domain]}: ${SEVERITY_LABEL[severity]}. Show details`}
                class={`flex h-full w-full flex-col items-start rounded-md border px-3 py-2.5 text-left transition-colors hover:border-ink-faint ${TILE[severity]} ${
                  escalated.has(domain) ? 'first-out' : ''
                }`}
              >
                <span class={`font-mono text-eyebrow uppercase ${lit ? 'font-semibold' : ''}`}>{DOMAIN_LABEL[domain]}</span>
                <span class={`mt-1 text-sm ${lit ? 'font-semibold' : ''}`}>{STATE_WORD[severity]}</span>
              </button>
            </li>
          );
        })}
      </ul>
    </nav>
  );
}
