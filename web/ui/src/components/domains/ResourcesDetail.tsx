import type { HealthState, NetworkInterface } from '../../types/health';
import { formatBytes, formatMiB, formatSpeed, formatTemperature } from '../../lib/format';
import { resourceFlag } from '../../lib/reasons';
import { Meter } from '../Meter';
import { SubHeading } from '../Facts';
import { SeverityMark } from '../SeverityMark';

export function ResourcesDetail({ state }: { state: HealthState }) {
  const { cpu, memory, storage = [], thermal = [], network } = state.resources;
  const memPct = memory.mem_total_mb > 0 ? (memory.mem_used_mb / memory.mem_total_mb) * 100 : 0;

  return (
    <div class="grid grid-cols-1 gap-6 md:grid-cols-2">
      <section aria-label="CPU and memory">
        <SubHeading>CPU load</SubHeading>
        <dl class="mb-5 grid grid-cols-3 gap-2">
          {([['1 min', cpu.load1], ['5 min', cpu.load5], ['15 min', cpu.load15]] as const).map(([label, value]) => (
            <div key={label}>
              <dt class="text-xs text-ink-muted">{label}</dt>
              <dd class="font-mono text-lg">{value.toFixed(2)}</dd>
            </div>
          ))}
        </dl>

        <SubHeading>Memory</SubHeading>
        <Meter
          label="RAM"
          name="Memory used"
          percent={memPct}
          valueText={`${formatMiB(memory.mem_used_mb)} of ${formatMiB(memory.mem_total_mb)}`}
          flagged={resourceFlag(state, 'mem_used_high')}
          hint={memory.swap_used_mb > 0 ? `${formatMiB(memory.swap_used_mb)} swap in use` : null}
        />
      </section>

      <section aria-label="Storage and temperature">
        {storage.length > 0 && (
          <>
            <SubHeading>Storage</SubHeading>
            <ul class="mb-5 space-y-3">
              {storage.map((mount) =>
                mount.available !== false && typeof mount.used_pct === 'number' ? (
                  <li key={mount.mount}>
                    <Meter
                      label={<span class="font-mono text-[0.8125rem]">{mount.mount}</span>}
                      name={`Storage used on ${mount.mount}`}
                      percent={mount.used_pct}
                      valueText={`${mount.used_pct}% used`}
                      flagged={resourceFlag(state, 'disk_used_high', mount.mount)}
                      hint={[mount.fs, typeof mount.avail_mb === 'number' ? `${formatMiB(mount.avail_mb)} free` : null]
                        .filter(Boolean)
                        .join(' · ')}
                    />
                  </li>
                ) : (
                  <UnavailableRow key={mount.mount} name={mount.mount} note="Mount could not be read" />
                ),
              )}
            </ul>
          </>
        )}

        {thermal.length > 0 && (
          <>
            <SubHeading>Temperature</SubHeading>
            <ul class="space-y-1.5">
              {thermal.map((sensor) => {
                if (sensor.available === false || typeof sensor.temp_c !== 'number') {
                  return <UnavailableRow key={sensor.sensor} name={sensor.sensor} note="Sensor could not be read" />;
                }
                const flag = resourceFlag(state, 'temp_high', sensor.sensor);
                return (
                  <li key={sensor.sensor} class="flex items-baseline justify-between gap-4 text-sm">
                    <span class="font-mono text-[0.8125rem]">{sensor.sensor}</span>
                    <span
                      class={`font-mono text-[0.8125rem] ${
                        flag === 'crit' ? 'font-semibold text-crit' : flag === 'warn' ? 'font-semibold text-warn' : ''
                      }`}
                    >
                      {formatTemperature(sensor.temp_c)}
                    </span>
                  </li>
                );
              })}
            </ul>
          </>
        )}
      </section>

      <section aria-label="Network interfaces" class="md:col-span-2">
        <SubHeading>Network</SubHeading>
        {network.length === 0 ? (
          <p class="text-sm text-ink-muted">No network interfaces reported.</p>
        ) : (
          <div class="-mx-1 overflow-x-auto px-1">
            <table class="w-full min-w-[36rem] text-left text-sm">
              <thead>
                <tr class="text-ink-muted">
                  <th scope="col" class="pb-1.5 pr-4 font-normal">Interface</th>
                  <th scope="col" class="pb-1.5 pr-4 font-normal">Link</th>
                  <th scope="col" class="pb-1.5 pr-4 font-normal">Address</th>
                  <th scope="col" class="pb-1.5 pr-4 text-right font-normal">Received</th>
                  <th scope="col" class="pb-1.5 pr-4 text-right font-normal">Sent</th>
                  <th scope="col" class="pb-1.5 text-right font-normal">Drops / errors</th>
                </tr>
              </thead>
              <tbody class="divide-y divide-line">
                {network.map((iface) => (
                  <InterfaceRow key={iface.ifname} iface={iface} />
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </div>
  );
}

function InterfaceRow({ iface }: { iface: NetworkInterface }) {
  const speed = formatSpeed(iface.speed_mbps);
  const drops = iface.rx_dropped + iface.tx_dropped;
  const errors = iface.rx_err + iface.tx_err;

  return (
    <tr>
      <th scope="row" class="py-1.5 pr-4 font-mono text-[0.8125rem] font-normal">{iface.ifname}</th>
      <td class="py-1.5 pr-4">
        {iface.link}
        {iface.link === 'up' && speed && (
          <span class="text-ink-muted">
            {' '}· {speed}
            {iface.duplex && iface.duplex !== 'unknown' ? ` ${iface.duplex}` : ''}
          </span>
        )}
      </td>
      <td class="py-1.5 pr-4 font-mono text-[0.8125rem]">{iface.ip ?? <span class="text-ink-faint">—</span>}</td>
      <td class="py-1.5 pr-4 text-right font-mono text-[0.8125rem]">{formatBytes(iface.rx_bytes)}</td>
      <td class="py-1.5 pr-4 text-right font-mono text-[0.8125rem]">{formatBytes(iface.tx_bytes)}</td>
      <td class={`py-1.5 text-right font-mono text-[0.8125rem] ${drops + errors > 0 ? 'font-semibold' : 'text-ink-muted'}`}>
        {drops} / {errors}
      </td>
    </tr>
  );
}

function UnavailableRow({ name, note }: { name: string; note: string }) {
  return (
    <li class="hatch-unavailable flex list-none items-center justify-between gap-4 rounded-md px-2 py-1.5 text-sm">
      <span class="font-mono text-[0.8125rem]">{name}</span>
      <span class="flex items-center gap-1.5 text-unavailable">
        <SeverityMark severity="unavailable" decorative />
        {note}
      </span>
    </li>
  );
}
