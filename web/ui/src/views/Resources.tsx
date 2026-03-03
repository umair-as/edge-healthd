import { Card } from '../components/Card';
import { ProgressBar } from '../components/ProgressBar';
import { Metric, MetricRow } from '../components/Metric';
import { healthState } from '../state/signals';

export function Resources() {
  const state = healthState.value;

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading...</p>
      </div>
    );
  }

  const { cpu, memory, storage, thermal } = state.resources;

  return (
    <div class="space-y-6">
      <h2 class="text-lg font-semibold">System Resources</h2>

      {/* CPU Load */}
      <Card title="CPU Load">
        <div class="grid grid-cols-3 gap-4">
          <Metric label="1 min" value={cpu.load1.toFixed(2)} />
          <Metric label="5 min" value={cpu.load5.toFixed(2)} />
          <Metric label="15 min" value={cpu.load15.toFixed(2)} />
        </div>
      </Card>

      {/* Memory */}
      <Card title="Memory">
        <div class="space-y-4">
          <ProgressBar
            value={memory.mem_used_mb}
            max={memory.mem_total_mb}
            label="RAM"
            thresholds={{ warn: 70, crit: 90 }}
          />
          <div class="grid grid-cols-2 gap-4 pt-2">
            <MetricRow label="Used" value={`${memory.mem_used_mb} MB`} />
            <MetricRow label="Total" value={`${memory.mem_total_mb} MB`} />
            <MetricRow label="Swap Used" value={`${memory.swap_used_mb} MB`} />
            <MetricRow label="Available" value={`${memory.mem_total_mb - memory.mem_used_mb} MB`} />
          </div>
        </div>
      </Card>

      {/* Storage */}
      {storage && storage.length > 0 && (
        <Card title="Storage">
          <div class="space-y-4">
            {storage.map((mount) => (
              <div key={mount.mount}>
                <ProgressBar
                  value={mount.used_pct}
                  label={mount.mount}
                  thresholds={{ warn: 80, crit: 95 }}
                />
                <div class="flex justify-between text-xs text-gray-500 dark:text-gray-400 mt-1">
                  <span>{mount.fs || 'unknown'}</span>
                  <span>{mount.avail_mb} MB available</span>
                </div>
              </div>
            ))}
          </div>
        </Card>
      )}

      {/* Thermal */}
      {thermal && thermal.length > 0 && (
        <Card title="Thermal Sensors">
          <div class="space-y-3">
            {thermal.map((sensor) => {
              const temp = sensor.temp_c;
              let colorClass = 'text-severity-ok';
              if (temp >= 80) {
                colorClass = 'text-severity-crit';
              } else if (temp >= 70) {
                colorClass = 'text-severity-warn';
              }

              return (
                <div key={sensor.sensor} class="flex items-center justify-between">
                  <span class="text-sm text-gray-600 dark:text-gray-400">{sensor.sensor}</span>
                  <span class={`font-medium ${colorClass}`}>{temp.toFixed(1)}°C</span>
                </div>
              );
            })}
          </div>
        </Card>
      )}
    </div>
  );
}
