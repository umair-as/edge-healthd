import type { BootStatus } from '../../types/health';
import { formatDuration, formatTimestamp } from '../../lib/format';
import { Facts } from '../Facts';

export function BootDetail({ boot }: { boot: BootStatus }) {
  return (
    <Facts
      facts={[
        { label: 'Boot', value: boot.boot_ok ? 'Completed' : 'Failed' },
        { label: 'Uptime', value: formatDuration(boot.uptime) },
        { label: 'Booted at', value: formatTimestamp(boot.last_boot_at) },
        { label: 'Failed boots', value: String(boot.boot_fail_count) },
        { label: 'Last reboot reason', value: boot.last_reboot_reason },
        { label: 'Boot ID', value: boot.boot_id, mono: true },
      ]}
    />
  );
}
