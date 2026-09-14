import type { HealthState } from '../types/health';

export function DeviceFooter({ state }: { state: HealthState | null }) {
  return (
    <footer class="mt-10 border-t border-line">
      <div class="mx-auto flex max-w-5xl flex-wrap items-baseline justify-between gap-x-6 gap-y-2 px-4 py-5 text-xs text-ink-muted md:px-6">
        {state ? (
          <p class="font-mono">
            {state.device.device_id} · {state.device.os.distro}
            {state.device.os.version ? ` ${state.device.os.version}` : ''} · kernel {state.device.os.kernel} · schema{' '}
            {state.schema_version}
          </p>
        ) : (
          <span />
        )}
        <p class="flex gap-4">
          <a class="underline-offset-2 hover:text-ink hover:underline" href="/api/health">
            Raw JSON
          </a>
          <a
            class="underline-offset-2 hover:text-ink hover:underline"
            href="https://github.com/umair-as/edge-healthd"
            target="_blank"
            rel="noopener noreferrer"
          >
            Source
          </a>
        </p>
      </div>
    </footer>
  );
}
