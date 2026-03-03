import { Card } from '../components/Card';
import { StatusBadge } from '../components/StatusBadge';
import { ProgressBar } from '../components/ProgressBar';
import { Metric } from '../components/Metric';
import {
  healthState,
  overallSeverity,
  uptimeFormatted,
  cpuLoadFormatted,
  memoryUsedPercent,
  servicesOkCount,
  servicesTotalCount,
} from '../state/signals';

export function Dashboard() {
  const state = healthState.value;

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading health data...</p>
      </div>
    );
  }

  const reasons = state.summary.reasons || [];
  const topServices = state.services.units.slice(0, 5);

  return (
    <div class="space-y-6">
      {/* Overall Status */}
      <Card>
        <div class="flex items-center justify-between">
          <div>
            <h2 class="text-lg font-semibold mb-1">System Health</h2>
            <StatusBadge severity={overallSeverity.value} size="lg" pulse />
          </div>
          <div class="text-right">
            <p class="text-sm text-gray-500 dark:text-gray-400">Last updated</p>
            <p class="text-sm">{new Date(state.generated_at).toLocaleTimeString()}</p>
          </div>
        </div>

        {reasons.length > 0 && overallSeverity.value !== 'ok' && (
          <div class="mt-4 pt-4 border-t border-gray-200 dark:border-dark-border">
            <p class="text-sm text-gray-600 dark:text-gray-400 mb-2">Issues:</p>
            <div class="flex flex-wrap gap-2">
              {reasons.map((reason) => (
                <span
                  key={reason}
                  class="px-2 py-1 text-xs bg-gray-100 dark:bg-dark-card rounded-full"
                >
                  {reason}
                </span>
              ))}
            </div>
          </div>
        )}
      </Card>

      {/* Quick Stats */}
      <div class="grid grid-cols-2 md:grid-cols-4 gap-4">
        <Card hover>
          <Metric
            label="Uptime"
            value={uptimeFormatted.value}
            icon={
              <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
            }
          />
        </Card>

        <Card hover>
          <Metric
            label="CPU Load"
            value={cpuLoadFormatted.value}
            icon={
              <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z" />
              </svg>
            }
          />
        </Card>

        <Card hover>
          <Metric
            label="Memory"
            value={`${memoryUsedPercent.value}%`}
            subtext={`${state.resources.memory.mem_used_mb} / ${state.resources.memory.mem_total_mb} MB`}
            icon={
              <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10" />
              </svg>
            }
          />
        </Card>

        <Card hover>
          <Metric
            label="Services"
            value={`${servicesOkCount.value}/${servicesTotalCount.value}`}
            subtext="healthy"
            icon={
              <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M5 12h14M5 12a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v4a2 2 0 01-2 2M5 12a2 2 0 00-2 2v4a2 2 0 002 2h14a2 2 0 002-2v-4a2 2 0 00-2-2m-2-4h.01M17 16h.01" />
              </svg>
            }
          />
        </Card>
      </div>

      {/* Memory Progress */}
      <Card title="Memory Usage">
        <ProgressBar
          value={memoryUsedPercent.value}
          label="RAM"
          thresholds={{ warn: 70, crit: 90 }}
        />
        {state.resources.memory.swap_used_mb > 0 && (
          <div class="mt-3">
            <ProgressBar
              value={state.resources.memory.swap_used_mb}
              max={1024}
              label="Swap"
              thresholds={{ warn: 256, crit: 512 }}
            />
          </div>
        )}
      </Card>

      {/* Services Summary */}
      <Card title="Services">
        <div class="divide-y divide-gray-200 dark:divide-dark-border">
          {topServices.map((service) => (
            <div key={service.name} class="py-2 flex items-center justify-between">
              <div class="flex items-center gap-2">
                <StatusBadge severity={service.severity} size="sm" label={service.state} />
                <span class="text-sm font-medium">{service.name}</span>
              </div>
              {service.restart_count > 0 && (
                <span class="text-xs text-gray-500 dark:text-gray-400">
                  {service.restart_count} restarts
                </span>
              )}
            </div>
          ))}
        </div>
        {state.services.units.length > 5 && (
          <p class="text-xs text-gray-500 dark:text-gray-400 mt-2">
            +{state.services.units.length - 5} more services
          </p>
        )}
      </Card>
    </div>
  );
}
