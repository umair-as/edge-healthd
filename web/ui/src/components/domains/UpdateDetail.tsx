import type { UpdateStatus } from '../../types/health';
import { formatTimestamp } from '../../lib/format';
import { Facts } from '../Facts';

const RESULT_LABEL = { success: 'Succeeded', failed: 'Failed', unknown: 'Unknown' } as const;

export function UpdateDetail({ update }: { update: UpdateStatus }) {
  const last = update.last_update;

  return (
    <Facts
      facts={[
        { label: 'Active slot', value: update.active_slot, mono: true },
        { label: 'Last update', value: last ? last.id : 'No update history', mono: Boolean(last) },
        { label: 'Result', value: last ? RESULT_LABEL[last.result] ?? last.result : null },
        { label: 'Installed', value: last?.installed_at ? formatTimestamp(last.installed_at) : null },
        { label: 'Detail', value: last?.detail },
      ]}
    />
  );
}
