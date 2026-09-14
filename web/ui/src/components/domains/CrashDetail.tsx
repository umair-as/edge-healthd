import type { CrashStatus } from '../../types/health';
import { formatBytes, formatTimestamp } from '../../lib/format';
import { Facts, SubHeading } from '../Facts';

export function CrashDetail({ crash }: { crash: CrashStatus | undefined }) {
  if (!crash) {
    return <p class="text-sm text-ink-muted">This snapshot doesn't include crash data.</p>;
  }

  if (!crash.present) {
    return <p class="text-sm text-ink-muted">No kernel crash records in pstore.</p>;
  }

  return (
    <div class="space-y-5">
      <Facts
        facts={[
          { label: 'Acknowledged', value: crash.acknowledged ? 'Yes' : 'No' },
          { label: 'Last panic', value: crash.last_panic_at ? formatTimestamp(crash.last_panic_at) : null },
          { label: 'Source', value: crash.source, mono: true },
          { label: 'Fingerprint', value: crash.fingerprint, mono: true },
        ]}
      />

      {crash.artifacts.length > 0 && (
        <section aria-label="Crash records">
          <SubHeading>Records</SubHeading>
          <ul class="divide-y divide-line text-sm">
            {crash.artifacts.map((a) => (
              <li key={a.name} class="flex flex-wrap items-baseline justify-between gap-x-4 py-1.5">
                <span class="font-mono text-[0.8125rem]">{a.name}</span>
                <span class="text-ink-muted">
                  {formatBytes(a.size_bytes)}
                  {a.mtime ? ` · ${formatTimestamp(a.mtime)}` : ''}
                </span>
              </li>
            ))}
          </ul>
        </section>
      )}

      {!crash.acknowledged && crash.fingerprint && (
        <section aria-label="How to acknowledge">
          <SubHeading>Acknowledge on the gateway</SubHeading>
          <p class="mb-2 text-sm text-ink-muted">
            Once the records are collected, acknowledge them so the crash domain returns to OK:
          </p>
          <pre
            class="overflow-x-auto rounded-md bg-raised p-3 font-mono text-xs"
            role="region"
            aria-label="Acknowledge command"
            tabIndex={0}
          >
            {`busctl call edge.health /edge/health/manager edge.health.Manager AcknowledgeCrash s ${crash.fingerprint}`}
          </pre>
        </section>
      )}
    </div>
  );
}
