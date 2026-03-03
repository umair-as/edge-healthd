import { Card } from '../components/Card';
import { StatusBadge } from '../components/StatusBadge';
import { MetricRow } from '../components/Metric';
import { healthState } from '../state/signals';
import type { Severity, UpdateResult } from '../types/health';

function resultToSeverity(result?: UpdateResult): Severity {
  if (!result) return 'unknown';
  switch (result) {
    case 'success': return 'ok';
    case 'failed': return 'crit';
    default: return 'unknown';
  }
}

export function Update() {
  const state = healthState.value;

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading...</p>
      </div>
    );
  }

  const { update } = state;

  return (
    <div class="space-y-4">
      <div class="flex items-center justify-between">
        <h2 class="text-lg font-semibold">System Update</h2>
        <StatusBadge severity={update.overall} size="md" />
      </div>

      {/* Active Slot */}
      {update.active_slot && (
        <Card>
          <div class="flex items-center justify-between">
            <div>
              <p class="text-sm text-gray-500 dark:text-gray-400">Active Slot</p>
              <p class="text-2xl font-bold">{update.active_slot.toUpperCase()}</p>
            </div>
            <div class="w-16 h-16 flex items-center justify-center">
              <svg class="w-12 h-12 text-gray-400 dark:text-gray-500" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z" />
              </svg>
            </div>
          </div>
        </Card>
      )}

      {/* Last Update */}
      {update.last_update ? (
        <Card title="Last Update">
          <div class="space-y-3">
            <div class="flex items-center justify-between">
              <span class="text-gray-600 dark:text-gray-400">Result</span>
              <StatusBadge
                severity={resultToSeverity(update.last_update.result)}
                size="md"
                label={update.last_update.result}
              />
            </div>

            <MetricRow label="Update ID" value={update.last_update.id} />

            {update.last_update.installed_at && (
              <MetricRow
                label="Installed"
                value={new Date(update.last_update.installed_at).toLocaleString()}
              />
            )}

            {update.last_update.detail && (
              <div class="pt-2 border-t border-gray-200 dark:border-dark-border">
                <p class="text-sm text-gray-500 dark:text-gray-400 mb-1">Details</p>
                <p class="text-sm">{update.last_update.detail}</p>
              </div>
            )}
          </div>
        </Card>
      ) : (
        <Card>
          <p class="text-center text-gray-500 dark:text-gray-400">
            No update history available
          </p>
        </Card>
      )}

      {/* Boot Status */}
      <Card title="Boot Status">
        <div class="space-y-2">
          <div class="flex items-center justify-between">
            <span class="text-gray-600 dark:text-gray-400">Boot OK</span>
            <StatusBadge
              severity={state.boot.boot_ok ? 'ok' : 'crit'}
              size="sm"
              label={state.boot.boot_ok ? 'Yes' : 'No'}
            />
          </div>
          <MetricRow label="Boot ID" value={state.boot.boot_id.slice(0, 8) + '...'} />
          <MetricRow label="Boot Failures" value={state.boot.boot_fail_count} />
          {state.boot.last_reboot_reason && (
            <MetricRow label="Last Reboot" value={state.boot.last_reboot_reason} />
          )}
        </div>
      </Card>
    </div>
  );
}
