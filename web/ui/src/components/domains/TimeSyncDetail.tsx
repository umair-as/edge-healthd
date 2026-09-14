import type { TimeSyncState, TimeSyncStatus } from '../../types/health';
import { formatNanoseconds, formatTimestamp } from '../../lib/format';
import { Facts, SubHeading } from '../Facts';

const SYNC_STATE: Record<TimeSyncState, string> = {
  locked: 'Locked',
  free_running: 'Free running',
  holdover: 'Holdover',
  unknown: 'Unknown',
};

export function TimeSyncDetail({ timeSync }: { timeSync: TimeSyncStatus }) {
  const { ntp, ptp, rtc } = timeSync;

  return (
    <div class="grid grid-cols-1 gap-6 md:grid-cols-3">
      <section aria-label="Source">
        <SubHeading>Source</SubHeading>
        <p class="text-sm">
          {timeSync.source === 'none' ? (
            <span class="font-medium">No synchronization source is active</span>
          ) : (
            <span class="font-mono">{timeSync.source.toUpperCase()}</span>
          )}
        </p>
      </section>

      {ntp && (
        <section aria-label="NTP">
          <SubHeading>NTP</SubHeading>
          <Facts
            facts={[
              { label: 'Enabled', value: ntp.enabled ? 'Yes' : 'No' },
              { label: 'State', value: ntp.enabled && ntp.state ? SYNC_STATE[ntp.state] : null },
              { label: 'Last sync', value: ntp.last_sync_at ? formatTimestamp(ntp.last_sync_at) : null },
            ]}
          />
        </section>
      )}

      {ptp?.enabled && (
        <section aria-label="PTP">
          <SubHeading>PTP</SubHeading>
          <Facts
            facts={[
              { label: 'Interface', value: ptp.interface, mono: true },
              { label: 'Role', value: ptp.role },
              { label: 'State', value: ptp.state ? SYNC_STATE[ptp.state] : null },
              { label: 'Offset', value: formatNanoseconds(ptp.offset_ns), mono: true },
              { label: 'RMS', value: ptp.rms_ns !== null && ptp.rms_ns !== undefined ? formatNanoseconds(ptp.rms_ns) : null, mono: true },
              { label: 'Last sync', value: ptp.last_sync_at ? formatTimestamp(ptp.last_sync_at) : null },
            ]}
          />
        </section>
      )}

      {rtc && (
        <section aria-label="Hardware clock">
          <SubHeading>Hardware clock</SubHeading>
          {rtc.enabled ? (
            <Facts
              facts={[
                { label: 'Set clock at boot', value: rtc.hctosys === undefined ? null : rtc.hctosys ? 'Yes' : 'No' },
                { label: 'Battery', value: rtc.voltage_mv !== undefined ? `${(rtc.voltage_mv / 1000).toFixed(2)} V` : null, mono: true },
                {
                  label: 'Drift',
                  value: rtc.drift_sec !== undefined ? `${rtc.drift_sec > 0 ? '+' : ''}${rtc.drift_sec.toFixed(1)} s` : null,
                  mono: true,
                },
              ]}
            />
          ) : (
            <p class="text-sm text-ink-muted">No RTC detected.</p>
          )}
        </section>
      )}
    </div>
  );
}
