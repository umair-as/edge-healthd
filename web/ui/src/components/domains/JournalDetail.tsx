import type { JournalStatus } from '../../types/health';
import { plural } from '../../lib/format';

export function JournalDetail({ journal }: { journal: JournalStatus }) {
  if (journal.recent_errors.length === 0) {
    return (
      <p class="text-sm text-ink-muted">
        {journal.error_count === 0
          ? 'No error-priority entries in the scan window.'
          : `${plural(journal.error_count, 'error')} counted; no excerpts retained.`}
      </p>
    );
  }

  return (
    <div>
      <p class="mb-2 text-sm text-ink-muted">
        {plural(journal.error_count, 'error-priority entry', 'error-priority entries')} in the scan window. Most recent:
      </p>
      <div
        class="max-h-72 overflow-auto rounded-md bg-raised p-3"
        role="region"
        aria-label="Recent journal errors"
        tabIndex={0}
      >
        <ol class="space-y-1 font-mono text-xs leading-relaxed">
          {journal.recent_errors.map((line, i) => (
            <li key={i} class="break-all">
              {line}
            </li>
          ))}
        </ol>
      </div>
    </div>
  );
}
