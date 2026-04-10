import { Card } from '../components/Card';
import { StatusBadge } from '../components/StatusBadge';
import { healthState } from '../state/signals';

export function Journal() {
  const state = healthState.value;

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading...</p>
      </div>
    );
  }

  const { journal } = state;

  return (
    <div class="space-y-4">
      <div class="flex items-center justify-between">
        <h2 class="text-lg font-semibold">System Journal</h2>
        <StatusBadge severity={journal.overall} size="md" />
      </div>

      {/* Summary */}
      <Card>
        <div class="flex items-center justify-between">
          <span class="text-gray-600 dark:text-gray-400">Errors in scan window</span>
          <span class={`text-2xl font-bold ${
            journal.error_count === 0
              ? 'text-severity-ok'
              : journal.overall === 'crit'
              ? 'text-severity-crit'
              : 'text-severity-warn'
          }`}>
            {journal.error_count}
          </span>
        </div>
      </Card>

      {/* Log entries */}
      {journal.recent_errors.length > 0 ? (
        <Card title="Recent Errors">
          <div class="space-y-1">
            {journal.recent_errors.map((line, i) => (
              <div
                key={i}
                class="font-mono text-xs py-1.5 px-2 rounded bg-gray-50 dark:bg-dark-bg text-gray-700 dark:text-gray-300 break-all leading-relaxed"
              >
                {line}
              </div>
            ))}
          </div>
        </Card>
      ) : (
        <Card>
          <p class="text-center text-severity-ok py-4">No errors in scan window</p>
        </Card>
      )}
    </div>
  );
}
