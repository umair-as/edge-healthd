import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { fixtures } from '../test/fixtures';
import { POLL_INTERVAL_MS, requestCollection, startHealthFeed, type FeedDeps } from './feed';
import { connection, fromCache, problem, snapshot } from './store';

class FakeSocket {
  static instances: FakeSocket[] = [];
  onopen: (() => void) | null = null;
  onmessage: ((e: { data: string }) => void) | null = null;
  onclose: (() => void) | null = null;
  closed = false;
  constructor(public url: string) {
    FakeSocket.instances.push(this);
  }
  close() {
    this.closed = true;
  }
  open() {
    this.onopen?.();
  }
  push(data: unknown) {
    this.onmessage?.({ data: JSON.stringify(data) });
  }
  drop() {
    this.onclose?.();
  }
}

const json = (body: unknown, status = 200) =>
  Promise.resolve(new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } }));

function deps(fetchImpl: FeedDeps['fetch']): FeedDeps {
  return {
    fetch: fetchImpl,
    WebSocket: FakeSocket as unknown as typeof WebSocket,
    location: { protocol: 'http:', host: 'gateway.local' },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
}

let stop: (() => void) | null = null;

beforeEach(() => {
  vi.useFakeTimers();
  FakeSocket.instances = [];
});

afterEach(() => {
  stop?.();
  stop = null;
  vi.useRealTimers();
});

describe('startHealthFeed', () => {
  it('shows the first HTTP snapshot, then switches to live push', async () => {
    const fetchMock = vi.fn(() => json(fixtures.healthy()));
    stop = startHealthFeed(deps(fetchMock));
    await vi.waitFor(() => expect(snapshot.value).not.toBeNull());
    expect(connection.value).toBe('polling');

    const ws = FakeSocket.instances[0];
    expect(ws.url).toBe('ws://gateway.local/ws/health');
    ws.open();
    expect(connection.value).toBe('live');

    ws.push({ ...fixtures.critical(), generated_at: '2026-07-20T12:05:00Z' });
    expect(snapshot.value?.summary.severity).toBe('crit');
  });

  it('falls back to polling and reconnects with backoff when the socket drops', async () => {
    const fetchMock = vi.fn(() => json(fixtures.healthy()));
    stop = startHealthFeed(deps(fetchMock));
    FakeSocket.instances[0].open();
    FakeSocket.instances[0].drop();
    expect(connection.value).toBe('polling');

    const callsBefore = fetchMock.mock.calls.length;
    await vi.advanceTimersByTimeAsync(POLL_INTERVAL_MS);
    expect(fetchMock.mock.calls.length).toBeGreaterThan(callsBefore);
    expect(FakeSocket.instances.length).toBe(2); // reconnect after 1 s
  });

  it('reports a missing state file distinctly from an unreachable server', async () => {
    stop = startHealthFeed(deps(vi.fn(() => json({ error: 'state not available' }, 503))));
    await vi.waitFor(() => expect(problem.value).toEqual({ kind: 'no-snapshot' }));
  });

  it('reports an unreachable server', async () => {
    stop = startHealthFeed(deps(vi.fn(() => Promise.reject(new TypeError('Failed to fetch')))));
    await vi.waitFor(() => expect(connection.value).toBe('unreachable'));
    expect(problem.value).toEqual({ kind: 'unreachable' });
  });

  it('restores the cached snapshot immediately, marked as cached', () => {
    localStorage.setItem('edge-healthd-state', JSON.stringify({ state: fixtures.degraded(), firstSeenAt: 1 }));
    stop = startHealthFeed(deps(vi.fn(() => new Promise<Response>(() => {}))));
    expect(snapshot.value?.summary.severity).toBe('warn');
    expect(fromCache.value).toBe(true);
  });

  it('stops cleanly', () => {
    const s = startHealthFeed(deps(vi.fn(() => json(fixtures.healthy()))));
    s();
    expect(FakeSocket.instances[0].closed).toBe(true);
  });
});

describe('requestCollection', () => {
  it.each<[string, () => Promise<Response>, string]>([
    ['accepted', () => json({ triggered: true }), 'requested'],
    ['rate-limited', () => json({ triggered: false }), 'rate-limited'],
    ['D-Bus down', () => json({ error: 'D-Bus system bus unavailable' }, 503), 'unavailable'],
    ['network error', () => Promise.reject(new TypeError('offline')), 'unavailable'],
  ])('maps %s', async (_name, response, expected) => {
    const fetchMock = vi.fn(response);
    expect(await requestCollection(fetchMock)).toBe(expected);
    expect(fetchMock).toHaveBeenCalledWith('/api/trigger', expect.objectContaining({ method: 'POST' }));
  });
});
