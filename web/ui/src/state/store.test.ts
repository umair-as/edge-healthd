import { describe, expect, it } from 'vitest';
import { fixtures } from '../test/fixtures';
import { acceptSnapshot, escalated, fromCache, overall, problem, snapshot } from './store';

describe('acceptSnapshot', () => {
  it('applies a valid snapshot', () => {
    expect(acceptSnapshot(fixtures.degraded(), 'network')).toBe('accepted');
    expect(overall.value).toBe('warn');
  });

  it('keeps the last good snapshot when an update is invalid', () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    expect(acceptSnapshot({ schema: 'edge.health.state', schema_version: '9.0' }, 'network')).toBe('rejected');
    expect(snapshot.value?.summary.severity).toBe('ok');
    expect(problem.value).toMatchObject({ kind: 'invalid' });
  });

  it('treats a re-sent snapshot as unchanged and clears a stale problem', () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    problem.value = { kind: 'unreachable' };
    expect(acceptSnapshot(fixtures.healthy(), 'network')).toBe('unchanged');
    expect(problem.value).toBeNull();
  });

  it('records which domains got worse for the first-out flash', () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    const next = fixtures.degraded();
    next.generated_at = '2026-07-20T12:01:05Z';
    acceptSnapshot(next, 'network');
    expect([...escalated.value].sort()).toEqual(['journal', 'resources', 'services']);
  });

  it('marks cached snapshots until the network confirms', () => {
    acceptSnapshot(fixtures.healthy(), 'cache', 1000);
    expect(fromCache.value).toBe(true);
    acceptSnapshot(fixtures.healthy(), 'network');
    expect(fromCache.value).toBe(false);
  });

  it('ignores an invalid cache entry silently', () => {
    expect(acceptSnapshot({ nope: true }, 'cache')).toBe('rejected');
    expect(problem.value).toBeNull();
  });
});
