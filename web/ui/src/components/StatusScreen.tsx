import type { FeedProblem } from '../state/store';

// Full-page states for when there is no snapshot to show at all.
export function StatusScreen({ problem }: { problem: FeedProblem | null }) {
  const { title, body } = describe(problem);

  return (
    <section class="mx-auto max-w-xl py-24 text-center" aria-live="polite">
      {problem === null && (
        <div class="mx-auto mb-6 h-1 w-24 overflow-hidden rounded-full bg-line" aria-hidden="true">
          <div class="h-full w-1/3 animate-pulse rounded-full bg-ink-faint" />
        </div>
      )}
      <h2 class="text-2xl font-semibold tracking-tight">{title}</h2>
      <p class="mt-2 text-ink-muted">{body}</p>
      {problem?.kind === 'invalid' && (
        <p class="mt-4 text-sm">
          <a class="text-focus underline underline-offset-2" href="/api/health">
            View the raw snapshot
          </a>
        </p>
      )}
    </section>
  );
}

function describe(problem: FeedProblem | null): { title: string; body: string } {
  switch (problem?.kind) {
    case undefined:
      return { title: 'Connecting to the gateway…', body: 'Fetching the latest health snapshot.' };
    case 'no-snapshot':
      return {
        title: 'No snapshot yet',
        body: "edge-healthd hasn't written its state file yet. This page updates as soon as the first collection finishes.",
      };
    case 'unreachable':
      return {
        title: "Can't reach the gateway",
        body: `The web server at ${window.location.host} isn't responding. Retrying every 5 seconds.`,
      };
    case 'invalid':
      return { title: "This snapshot can't be displayed", body: `${problem.message.charAt(0).toUpperCase()}${problem.message.slice(1)}.` };
  }
}
