import type { ComponentChildren } from 'preact';
import { formatAge } from '../lib/format';
import { fromCache, isSilent, problem, silenceSeconds } from '../state/store';

// Banners that qualify the snapshot on screen. They say what is wrong with
// the *data*, so the verdict below is never mistaken for current truth.
export function Notices() {
  const p = problem.value;
  const age = silenceSeconds.value;
  const notices: ComponentChildren[] = [];

  if (fromCache.value) {
    notices.push(
      <Notice key="cache" tone="crit" title="Can't reach the gateway">
        Showing the snapshot this browser saved {age === null ? 'earlier' : formatAge(age)}. Retrying every 5 seconds.
      </Notice>,
    );
  } else if (p?.kind === 'unreachable') {
    notices.push(
      <Notice key="unreachable" tone="crit" title="Lost contact with the gateway">
        Showing the last snapshot received. Retrying every 5 seconds.
      </Notice>,
    );
  } else if (isSilent.value) {
    notices.push(
      <Notice key="silent" tone="stale" title={`No new snapshot for ${age === null ? 'a while' : formatAge(age).replace(' ago', '')}`}>
        The web server is responding, but edge-healthd hasn't written a new snapshot. It may have stopped collecting;
        everything below is from the last one.
      </Notice>,
    );
  }

  if (p?.kind === 'invalid') {
    notices.push(
      <Notice key="invalid" tone="warn" title="The latest update was rejected">
        {capitalize(p.message)}. Showing the previous snapshot.
      </Notice>,
    );
  }

  if (notices.length === 0) return null;
  return <div class="space-y-2 pt-4">{notices}</div>;
}

const TONE = {
  crit: 'border-crit text-crit',
  warn: 'border-warn text-warn',
  stale: 'border-stale text-stale hatch-stale',
} as const;

function Notice({ tone, title, children }: { tone: keyof typeof TONE; title: string; children: ComponentChildren }) {
  return (
    <div role="alert" class={`rounded-md border-l-4 bg-panel px-4 py-3 ${TONE[tone]}`}>
      <p class="text-sm font-semibold">{title}</p>
      <p class="mt-0.5 text-sm text-ink">{children}</p>
    </div>
  );
}

function capitalize(s: string): string {
  return s.charAt(0).toUpperCase() + s.slice(1);
}
