import { useEffect, useRef, useState } from 'preact/hooks';
import type { Domain, HealthState, Severity } from '../types/health';
import { formatAge } from '../lib/format';
import { SEVERITY_LABEL } from '../lib/severity';
import { verdictFor } from '../lib/verdict';
import { requestCollection, type CollectOutcome } from '../state/feed';
import { silenceSeconds } from '../state/store';
import { SEVERITY_TEXT, SeverityMark } from './SeverityMark';

interface VerdictProps {
  state: HealthState;
  overall: Severity;
  severities: Record<Domain, Severity>;
}

export function Verdict({ state, overall, severities }: VerdictProps) {
  const { headline, detail } = verdictFor(severities);
  const age = silenceSeconds.value;
  const reasons = state.summary.reasons;

  return (
    <section aria-labelledby="verdict-heading" class="pt-8 pb-6">
      <p class={`flex items-center gap-2 font-mono text-xs font-semibold uppercase tracking-[0.08em] ${SEVERITY_TEXT[overall]}`}>
        <SeverityMark severity={overall} decorative />
        {SEVERITY_LABEL[overall]}
      </p>

      <div class="mt-2 flex flex-wrap items-end justify-between gap-x-8 gap-y-4">
        <div class="min-w-0">
          <h2 id="verdict-heading" class="text-3xl font-semibold tracking-tight md:text-4xl">
            {headline}
          </h2>
          {detail && <p class="mt-1.5 text-ink-muted">{detail}</p>}
        </div>
        <CollectButton />
      </div>

      <p class="mt-4 font-mono text-xs text-ink-muted">
        Updated {age === null ? '—' : formatAge(age)} · cycle {state.cycle} · snapshot{' '}
        <time dateTime={state.generated_at}>{state.generated_at}</time>
      </p>

      {reasons.length > 0 && (
        <ul class="mt-4 flex flex-wrap gap-1.5" aria-label="Reported reasons">
          {reasons.map((r) => (
            <li key={r} class="rounded border border-line bg-panel px-1.5 py-0.5 font-mono text-xs text-ink-muted">
              {r}
            </li>
          ))}
        </ul>
      )}

      {state.summary.notes && <p class="mt-3 text-sm text-ink-muted">{state.summary.notes}</p>}
    </section>
  );
}

const OUTCOME_MESSAGE: Record<CollectOutcome, string> = {
  requested: 'Collection requested. The page updates when the snapshot is written.',
  'rate-limited': 'A collection just ran. Try again in a few seconds.',
  unavailable: "Can't request a collection: the web server can't reach edge-healthd over D-Bus.",
};

function CollectButton() {
  const [pending, setPending] = useState(false);
  const [outcome, setOutcome] = useState<CollectOutcome | null>(null);
  const clearTimer = useRef<ReturnType<typeof setTimeout>>();

  useEffect(() => () => clearTimeout(clearTimer.current), []);

  async function collect() {
    setPending(true);
    setOutcome(null);
    const result = await requestCollection();
    setPending(false);
    setOutcome(result);
    clearTimeout(clearTimer.current);
    clearTimer.current = setTimeout(() => setOutcome(null), 6000);
  }

  return (
    <div class="flex flex-col items-start gap-1.5 md:items-end">
      <button
        type="button"
        onClick={collect}
        disabled={pending}
        class="rounded-md border border-line bg-panel px-3 py-1.5 text-sm font-medium hover:bg-raised disabled:cursor-progress disabled:opacity-60"
      >
        {pending ? 'Collecting…' : 'Collect now'}
      </button>
      <p role="status" class="max-w-xs text-xs text-ink-muted md:text-right">
        {outcome ? OUTCOME_MESSAGE[outcome] : ''}
      </p>
    </div>
  );
}
