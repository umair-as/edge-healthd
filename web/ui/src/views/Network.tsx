import { Card } from '../components/Card';
import { StatusBadge } from '../components/StatusBadge';
import { MetricRow } from '../components/Metric';
import { healthState } from '../state/signals';
import type { Severity, NetworkInterface } from '../types/health';

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function linkToSeverity(iface: NetworkInterface): Severity {
  if (iface.link === 'up') return 'ok';
  if (iface.link === 'down') return 'crit';
  return 'unknown';
}

export function Network() {
  const state = healthState.value;

  if (!state) {
    return (
      <div class="flex items-center justify-center h-64">
        <p class="text-gray-500 dark:text-gray-400">Loading...</p>
      </div>
    );
  }

  const interfaces = state.resources.network;

  return (
    <div class="space-y-4">
      <h2 class="text-lg font-semibold">Network Interfaces</h2>

      <div class="grid md:grid-cols-2 gap-4">
        {interfaces.map((iface) => (
          <Card key={iface.ifname}>
            {/* Header */}
            <div class="flex items-center justify-between mb-4">
              <div class="flex items-center gap-3">
                <StatusBadge severity={linkToSeverity(iface)} size="md" label={iface.link} />
                <span class="font-semibold text-lg">{iface.ifname}</span>
              </div>
              {iface.speed_mbps && (
                <span class="text-sm text-gray-500 dark:text-gray-400">
                  {iface.speed_mbps} Mbps
                  {iface.duplex && ` / ${iface.duplex}`}
                </span>
              )}
            </div>

            {/* IP */}
            {iface.ip && (
              <div class="mb-4 p-2 bg-gray-50 dark:bg-dark-card rounded">
                <span class="text-xs text-gray-500 dark:text-gray-400">IP Address</span>
                <p class="font-mono text-sm">{iface.ip}</p>
              </div>
            )}

            {/* Counters */}
            <div class="space-y-1">
              <div class="grid grid-cols-2 gap-4">
                <div>
                  <p class="text-xs text-gray-500 dark:text-gray-400 uppercase mb-2">Receive</p>
                  <MetricRow label="Bytes" value={formatBytes(iface.rx_bytes)} />
                  <MetricRow label="Packets" value={iface.rx_packets.toLocaleString()} />
                  <MetricRow
                    label="Dropped"
                    value={iface.rx_dropped.toLocaleString()}
                    className={iface.rx_dropped > 0 ? 'text-severity-warn' : ''}
                  />
                  <MetricRow
                    label="Errors"
                    value={iface.rx_err.toLocaleString()}
                    className={iface.rx_err > 0 ? 'text-severity-crit' : ''}
                  />
                </div>
                <div>
                  <p class="text-xs text-gray-500 dark:text-gray-400 uppercase mb-2">Transmit</p>
                  <MetricRow label="Bytes" value={formatBytes(iface.tx_bytes)} />
                  <MetricRow label="Packets" value={iface.tx_packets.toLocaleString()} />
                  <MetricRow
                    label="Dropped"
                    value={iface.tx_dropped.toLocaleString()}
                    className={iface.tx_dropped > 0 ? 'text-severity-warn' : ''}
                  />
                  <MetricRow
                    label="Errors"
                    value={iface.tx_err.toLocaleString()}
                    className={iface.tx_err > 0 ? 'text-severity-crit' : ''}
                  />
                </div>
              </div>
            </div>
          </Card>
        ))}
      </div>

      {interfaces.length === 0 && (
        <Card>
          <p class="text-center text-gray-500 dark:text-gray-400">No network interfaces found</p>
        </Card>
      )}
    </div>
  );
}
