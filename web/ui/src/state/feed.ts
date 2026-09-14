import { effect } from '@preact/signals';
import { loadCachedSnapshot, persistSnapshot } from './persistence';
import {
  acceptSnapshot,
  connection,
  firstSeenAt,
  reportProblem,
  setConnection,
  snapshot,
} from './store';

// Data feed: WebSocket push from the Go server, with HTTP polling while the
// socket is down. Transport lives here, not in components, and its browser
// dependencies are injectable so it can be tested without a network.

export interface FeedDeps {
  fetch: typeof fetch;
  WebSocket: typeof WebSocket;
  location: Pick<Location, 'protocol' | 'host'>;
  setTimeout: typeof setTimeout;
  clearTimeout: typeof clearTimeout;
  setInterval: typeof setInterval;
  clearInterval: typeof clearInterval;
}

export const API_URL = '/api/health';
export const POLL_INTERVAL_MS = 5_000;
const RECONNECT_INITIAL_MS = 1_000;
const RECONNECT_MAX_MS = 30_000;

export function startHealthFeed(deps: FeedDeps = browserDeps()): () => void {
  let ws: WebSocket | null = null;
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  let pollTimer: ReturnType<typeof setInterval> | null = null;
  let reconnectDelay = RECONNECT_INITIAL_MS;
  let stopped = false;

  const cached = loadCachedSnapshot();
  if (cached) acceptSnapshot(cached.state, 'cache', cached.firstSeenAt);

  // Cache every newly accepted snapshot.
  const stopPersisting = effect(() => {
    const s = snapshot.value;
    const seen = firstSeenAt.value;
    if (s && seen !== null) persistSnapshot(s, seen);
  });

  async function poll(): Promise<void> {
    try {
      const res = await deps.fetch(API_URL, { cache: 'no-store' });
      if (stopped) return;
      if (res.status === 503) {
        if (connection.value !== 'live') setConnection('polling');
        if (!snapshot.value) reportProblem({ kind: 'no-snapshot' });
        return;
      }
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      let body: unknown;
      try {
        body = await res.json();
      } catch {
        reportProblem({ kind: 'invalid', message: 'the response is not valid JSON' });
        return;
      }
      acceptSnapshot(body, 'network');
      if (connection.value !== 'live') setConnection('polling');
    } catch {
      if (stopped) return;
      if (connection.value !== 'live') {
        setConnection('unreachable');
        reportProblem({ kind: 'unreachable' });
      }
    }
  }

  function startPolling(): void {
    if (pollTimer !== null) return;
    pollTimer = deps.setInterval(() => void poll(), POLL_INTERVAL_MS);
  }

  function stopPolling(): void {
    if (pollTimer !== null) deps.clearInterval(pollTimer);
    pollTimer = null;
  }

  function connect(): void {
    if (stopped) return;
    const scheme = deps.location.protocol === 'https:' ? 'wss:' : 'ws:';
    let socket: WebSocket;
    try {
      socket = new deps.WebSocket(`${scheme}//${deps.location.host}/ws/health`);
    } catch {
      scheduleReconnect();
      return;
    }
    ws = socket;

    socket.onopen = () => {
      reconnectDelay = RECONNECT_INITIAL_MS;
      setConnection('live');
      stopPolling();
    };

    socket.onmessage = (event: MessageEvent) => {
      try {
        acceptSnapshot(JSON.parse(String(event.data)), 'network');
      } catch {
        reportProblem({ kind: 'invalid', message: 'a pushed update is not valid JSON' });
      }
    };

    socket.onclose = () => {
      ws = null;
      if (stopped) return;
      if (connection.value === 'live') setConnection('polling');
      scheduleReconnect();
    };
  }

  function scheduleReconnect(): void {
    if (stopped || reconnectTimer !== null) return;
    startPolling();
    reconnectTimer = deps.setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, RECONNECT_MAX_MS);
  }

  void poll();
  connect();

  return () => {
    stopped = true;
    stopPersisting();
    stopPolling();
    if (reconnectTimer !== null) deps.clearTimeout(reconnectTimer);
    if (ws) {
      ws.onclose = null;
      ws.close();
    }
  };
}

/** Ask the daemon to collect a snapshot now (via the server's D-Bus bridge). */
export type CollectOutcome = 'requested' | 'rate-limited' | 'unavailable';

export async function requestCollection(fetchImpl: typeof fetch = fetch): Promise<CollectOutcome> {
  try {
    const res = await fetchImpl('/api/trigger', { method: 'POST', headers: { 'X-Edge-Health': '1' } });
    if (!res.ok) return 'unavailable';
    const body = (await res.json()) as { triggered?: unknown };
    return body.triggered === true ? 'requested' : 'rate-limited';
  } catch {
    return 'unavailable';
  }
}

function browserDeps(): FeedDeps {
  return {
    fetch: window.fetch.bind(window),
    WebSocket: window.WebSocket,
    location: window.location,
    setTimeout: window.setTimeout.bind(window) as typeof setTimeout,
    clearTimeout: window.clearTimeout.bind(window) as typeof clearTimeout,
    setInterval: window.setInterval.bind(window) as typeof setInterval,
    clearInterval: window.clearInterval.bind(window) as typeof clearInterval,
  };
}
