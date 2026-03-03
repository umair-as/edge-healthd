import { Card } from '../components/Card';
import { StatusBadge } from '../components/StatusBadge';
import { MetricRow } from '../components/Metric';
import { healthState } from '../state/signals';
import type { Severity, TimeSyncState } from '../types/health';

function syncStateToSeverity(state?: TimeSyncState): Severity {
  if (!state) return 'unknown';
  switch (state) {
    case 'locked': return 'ok';
    case 'free_running': return 'warn';
    case 'holdover': return 'warn';
    default: return 'unknown';
  }
}

function formatOffset(ns: number | null | undefined): string {
  if (ns === null || ns === undefined) return '--';
  if (Math.abs(ns) < 1000) return `${ns} ns`;
  if (Math.abs(ns) < 1000000) return `${(ns / 1000).toFixed(2)} µs`;
  return `${(ns / 1000000).toFixed(2)} ms`;
}

export function TimeSync() {
  const state = healthState.value;

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading...</p>
      </div>
    );
  }

  const { time_sync } = state;

  return (
    <div class="space-y-4">
      <div class="flex items-center justify-between">
        <h2 class="text-lg font-semibold">Time Synchronization</h2>
        <StatusBadge severity={time_sync.overall} size="md" />
      </div>

      {/* Source */}
      <Card>
        <div class="flex items-center justify-between">
          <span class="text-gray-600 dark:text-gray-400">Active Source</span>
          <span class="font-semibold uppercase">{time_sync.source}</span>
        </div>
      </Card>

      {/* NTP Status */}
      {time_sync.ntp && (
        <Card title="NTP">
          <div class="space-y-3">
            <div class="flex items-center justify-between">
              <span class="text-gray-600 dark:text-gray-400">Status</span>
              <span class={time_sync.ntp.enabled ? 'text-severity-ok' : 'text-gray-500'}>
                {time_sync.ntp.enabled ? 'Enabled' : 'Disabled'}
              </span>
            </div>

            {time_sync.ntp.enabled && (
              <>
                <div class="flex items-center justify-between">
                  <span class="text-gray-600 dark:text-gray-400">Sync State</span>
                  <StatusBadge
                    severity={syncStateToSeverity(time_sync.ntp.state)}
                    size="sm"
                    label={time_sync.ntp.state || 'unknown'}
                  />
                </div>

                {time_sync.ntp.last_sync_at && (
                  <div class="flex items-center justify-between">
                    <span class="text-gray-600 dark:text-gray-400">Last Sync</span>
                    <span class="text-sm">{new Date(time_sync.ntp.last_sync_at).toLocaleString()}</span>
                  </div>
                )}
              </>
            )}
          </div>
        </Card>
      )}

      {/* PTP Status */}
      {time_sync.ptp && (
        <Card title="PTP (Precision Time Protocol)">
          <div class="space-y-3">
            <div class="flex items-center justify-between">
              <span class="text-gray-600 dark:text-gray-400">Status</span>
              <span class={time_sync.ptp.enabled ? 'text-severity-ok' : 'text-gray-500'}>
                {time_sync.ptp.enabled ? 'Enabled' : 'Disabled'}
              </span>
            </div>

            {time_sync.ptp.enabled && (
              <>
                {time_sync.ptp.interface && (
                  <MetricRow label="Interface" value={time_sync.ptp.interface} />
                )}

                <div class="flex items-center justify-between">
                  <span class="text-gray-600 dark:text-gray-400">Sync State</span>
                  <StatusBadge
                    severity={syncStateToSeverity(time_sync.ptp.state)}
                    size="sm"
                    label={time_sync.ptp.state || 'unknown'}
                  />
                </div>

                {time_sync.ptp.role && (
                  <MetricRow label="Role" value={time_sync.ptp.role} />
                )}

                <div class="pt-2 border-t border-gray-200 dark:border-dark-border">
                  <MetricRow label="Offset" value={formatOffset(time_sync.ptp.offset_ns)} />
                  {time_sync.ptp.rms_ns !== null && time_sync.ptp.rms_ns !== undefined && (
                    <MetricRow label="RMS" value={formatOffset(time_sync.ptp.rms_ns)} />
                  )}
                </div>

                {time_sync.ptp.last_sync_at && (
                  <div class="flex items-center justify-between pt-2">
                    <span class="text-gray-600 dark:text-gray-400">Last Sync</span>
                    <span class="text-sm">{new Date(time_sync.ptp.last_sync_at).toLocaleString()}</span>
                  </div>
                )}
              </>
            )}
          </div>
        </Card>
      )}

      {time_sync.source === 'none' && (
        <Card>
          <p class="text-center text-severity-warn">
            No time synchronization source is active
          </p>
        </Card>
      )}
    </div>
  );
}
