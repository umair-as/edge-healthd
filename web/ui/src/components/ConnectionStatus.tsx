import { isConnected, isOffline, timeSinceUpdate } from '../state/signals';

export function ConnectionStatus() {
  const offline = isOffline.value;
  const connected = isConnected.value;

  if (offline) {
    return (
      <div class="flex items-center gap-2 text-sm text-gray-500 dark:text-gray-400">
        <span class="relative flex h-2.5 w-2.5">
          <span class="h-2.5 w-2.5 rounded-full bg-gray-400" />
        </span>
        <span>Offline</span>
      </div>
    );
  }

  if (!connected) {
    return (
      <div class="flex items-center gap-2 text-sm text-severity-warn">
        <span class="relative flex h-2.5 w-2.5">
          <span class="animate-ping absolute inline-flex h-full w-full rounded-full bg-severity-warn opacity-75" />
          <span class="relative inline-flex rounded-full h-2.5 w-2.5 bg-severity-warn" />
        </span>
        <span>Reconnecting...</span>
      </div>
    );
  }

  return (
    <div class="flex items-center gap-2 text-sm text-gray-500 dark:text-gray-400">
      <span class="relative flex h-2.5 w-2.5">
        <span class="h-2.5 w-2.5 rounded-full bg-severity-ok" />
      </span>
      <span>{timeSinceUpdate.value}</span>
    </div>
  );
}
