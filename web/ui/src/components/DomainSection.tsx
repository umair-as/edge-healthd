import type { ComponentChildren } from 'preact';
import type { Domain, SectionFreshness, Severity } from '../types/health';
import { deviceAgeSeconds } from '../lib/clock';
import { DOMAIN_LABEL } from '../lib/domains';
import { formatAge } from '../lib/format';
import { SEVERITY_LABEL } from '../lib/severity';
import { deviceNow } from '../state/store';
import { SeverityMark } from './SeverityMark';

interface DomainSectionProps {
  domain: Domain;
  severity: Severity;
  summary: string;
  freshness: SectionFreshness | undefined;
  open: boolean;
  onToggle: () => void;
  children: ComponentChildren;
}

export function DomainSection({ domain, severity, summary, freshness, open, onToggle, children }: DomainSectionProps) {
  const headingId = `domain-${domain}-heading`;
  const panelId = `domain-${domain}-panel`;

  return (
    <section id={`domain-${domain}`} aria-labelledby={headingId} class="scroll-mt-4">
      <h3 id={headingId}>
        <button
          type="button"
          aria-expanded={open}
          aria-controls={panelId}
          onClick={onToggle}
          class="grid w-full grid-cols-[auto_1fr_auto] items-center gap-x-3 gap-y-0.5 px-4 py-3 text-left hover:bg-raised md:grid-cols-[auto_9rem_1fr_auto] md:px-5"
        >
          <SeverityMark severity={severity} decorative />
          <span class="font-semibold">
            {DOMAIN_LABEL[domain]}
            <span class="sr-only">, {SEVERITY_LABEL[severity]}</span>
          </span>
          <span class="col-start-2 row-start-2 min-w-0 truncate text-sm text-ink-muted md:col-start-3 md:row-start-1">
            {summary}
          </span>
          <span class="col-start-3 row-start-1 flex items-center gap-3 md:col-start-4">
            <Freshness freshness={freshness} />
            <svg
              viewBox="0 0 16 16"
              class={`h-4 w-4 text-ink-faint transition-transform ${open ? 'rotate-180' : ''}`}
              aria-hidden="true"
            >
              <path d="m4 6 4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" />
            </svg>
          </span>
        </button>
      </h3>

      <div id={panelId} hidden={!open} class="border-t border-line px-4 py-4 md:px-5 md:pl-[3.25rem]">
        {children}
      </div>
    </section>
  );
}

const FRESH_SECONDS = 60;

// Freshness uses device-clock ages (see lib/clock.ts), so a gateway with a
// wrong clock still shows correct relative ages.
function Freshness({ freshness }: { freshness: SectionFreshness | undefined }) {
  const age = deviceAgeSeconds(freshness?.collected_at, deviceNow.value);

  if (freshness?.stale) {
    return (
      <span class="whitespace-nowrap font-mono text-xs font-semibold text-stale">
        stale{age !== null ? ` · ${formatAge(age)}` : ''}
      </span>
    );
  }
  if (age === null) {
    return <span class="hidden whitespace-nowrap font-mono text-xs text-ink-faint sm:inline">not collected</span>;
  }
  // Fresh data stays quiet; the age only appears once it's worth reading.
  if (age < FRESH_SECONDS) return null;
  return (
    <span class="hidden whitespace-nowrap font-mono text-xs text-ink-faint sm:inline" title={freshness?.collected_at}>
      {formatAge(age)}
    </span>
  );
}
