import { describe, expect, it } from 'vitest';
import { isFault, isVisibilityLoss, needsAttention, normalizeSeverity, rank, worstOf } from './severity';

describe('severity ranking', () => {
  it('follows the schema roll-up order unknown < ok < stale < unavailable < warn < crit', () => {
    const order = ['unknown', 'ok', 'stale', 'unavailable', 'warn', 'crit'] as const;
    const ranks = order.map(rank);
    expect(ranks).toEqual([...ranks].sort((a, b) => a - b));
    expect(new Set(ranks).size).toBe(order.length);
  });

  it('never lets a visibility loss outrank a real fault', () => {
    expect(worstOf(['stale', 'warn', 'unavailable'])).toBe('warn');
    expect(worstOf(['ok', 'stale'])).toBe('stale');
  });

  it('keeps warm-up quiet: unknown ranks below ok', () => {
    expect(worstOf(['unknown', 'ok'])).toBe('ok');
    expect(worstOf([])).toBe('unknown');
  });
});

describe('severity classification', () => {
  it('separates faults from blind spots', () => {
    expect(isFault('crit')).toBe(true);
    expect(isFault('stale')).toBe(false);
    expect(isVisibilityLoss('unavailable')).toBe(true);
    expect(isVisibilityLoss('warn')).toBe(false);
  });

  it('flags anything above ok as needing attention', () => {
    expect(needsAttention('ok')).toBe(false);
    expect(needsAttention('unknown')).toBe(false);
    expect(needsAttention('stale')).toBe(true);
  });

  it('maps values from a newer schema to unknown instead of throwing', () => {
    expect(normalizeSeverity('degraded')).toBe('unknown');
    expect(normalizeSeverity(undefined)).toBe('unknown');
    expect(normalizeSeverity('warn')).toBe('warn');
  });
});
