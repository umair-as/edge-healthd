import { useState } from 'preact/hooks';
import { Card } from '../components/Card';
import { StatusBadge } from '../components/StatusBadge';
import { healthState } from '../state/signals';

export function Services() {
  const state = healthState.value;
  const [expandedService, setExpandedService] = useState<string | null>(null);

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading...</p>
      </div>
    );
  }

  const services = state.services.units;

  return (
    <div class="space-y-4">
      <div class="flex items-center justify-between">
        <h2 class="text-lg font-semibold">Monitored Services</h2>
        <StatusBadge severity={state.services.overall} size="md" />
      </div>

      <div class="space-y-3">
        {services.map((service) => {
          const isExpanded = expandedService === service.name;
          const hasLogs = service.log_excerpt && service.log_excerpt.length > 0;

          return (
            <Card key={service.name} className={isExpanded ? 'ring-2 ring-blue-500' : ''}>
              <button
                class="w-full text-left"
                onClick={() => setExpandedService(isExpanded ? null : service.name)}
              >
                <div class="flex items-center justify-between">
                  <div class="flex items-center gap-3">
                    <StatusBadge severity={service.severity} size="md" label={service.state} />
                    <span class="font-medium">{service.name}</span>
                  </div>
                  <div class="flex items-center gap-3">
                    {service.restart_count > 0 && (
                      <span class="text-xs px-2 py-1 bg-gray-100 dark:bg-dark-card rounded-full">
                        {service.restart_count} restarts
                      </span>
                    )}
                    {hasLogs && (
                      <svg
                        class={`w-4 h-4 text-gray-400 transition-transform ${isExpanded ? 'rotate-180' : ''}`}
                        fill="none"
                        viewBox="0 0 24 24"
                        stroke="currentColor"
                      >
                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 9l-7 7-7-7" />
                      </svg>
                    )}
                  </div>
                </div>
              </button>

              {/* Details */}
              <div class="mt-3 pt-3 border-t border-gray-200 dark:border-dark-border text-sm">
                <div class="grid grid-cols-2 gap-2">
                  {service.since && (
                    <div>
                      <span class="text-gray-500 dark:text-gray-400">Since: </span>
                      <span>{new Date(service.since).toLocaleString()}</span>
                    </div>
                  )}
                  {service.result && (
                    <div>
                      <span class="text-gray-500 dark:text-gray-400">Result: </span>
                      <span>{service.result}</span>
                    </div>
                  )}
                  {service.detail && (
                    <div class="col-span-2">
                      <span class="text-gray-500 dark:text-gray-400">Detail: </span>
                      <span>{service.detail}</span>
                    </div>
                  )}
                </div>
              </div>

              {/* Log excerpt */}
              {isExpanded && hasLogs && (
                <div class="mt-3 pt-3 border-t border-gray-200 dark:border-dark-border">
                  <p class="text-xs text-gray-500 dark:text-gray-400 mb-2">Recent logs:</p>
                  <pre class="text-xs bg-gray-100 dark:bg-dark-bg p-3 rounded overflow-x-auto max-h-48">
                    {service.log_excerpt!.join('\n')}
                  </pre>
                </div>
              )}
            </Card>
          );
        })}
      </div>

      {services.length === 0 && (
        <Card>
          <p class="text-center text-gray-500 dark:text-gray-400">No services configured</p>
        </Card>
      )}
    </div>
  );
}
