import { describe, expect, it } from 'vitest';
import { formatAge, formatBytes, formatDuration, formatMiB, formatNanoseconds, formatSpeed, plural } from './format';

describe('formatAge', () => {
  it.each([
    [0, 'just now'],
    [4.4, 'just now'],
    [12, '12 s ago'],
    [59, '59 s ago'],
    [60, '1 min ago'],
    [3599, '59 min ago'],
    [7200, '2 h ago'],
    [3 * 86400, '3 d ago'],
  ])('%s s → %s', (seconds, expected) => {
    expect(formatAge(seconds)).toBe(expected);
  });

  it('handles bad input without throwing', () => {
    expect(formatAge(Number.NaN)).toBe('—');
    expect(formatAge(-30)).toBe('just now');
  });
});

describe('formatDuration', () => {
  it('picks the two most useful units', () => {
    expect(formatDuration(45)).toBe('0m');
    expect(formatDuration(3 * 3600 + 5 * 60)).toBe('3h 5m');
    expect(formatDuration(2 * 86400 + 5 * 3600)).toBe('2d 5h');
  });
});

describe('sizes and rates', () => {
  it('formats bytes in binary units', () => {
    expect(formatBytes(512)).toBe('512 B');
    expect(formatBytes(1536)).toBe('1.5 KiB');
    expect(formatBytes(150 * 1024 * 1024)).toBe('150 MiB');
  });

  it('formats MiB, switching to GiB at 1024', () => {
    expect(formatMiB(512)).toBe('512 MiB');
    expect(formatMiB(3788)).toBe('3.7 GiB');
  });

  it('omits unknown or zero link speeds', () => {
    expect(formatSpeed(1000)).toBe('1 Gbit/s');
    expect(formatSpeed(100)).toBe('100 Mbit/s');
    expect(formatSpeed(null)).toBeNull();
    expect(formatSpeed(0)).toBeNull();
  });

  it('scales nanoseconds', () => {
    expect(formatNanoseconds(-850)).toBe('-850 ns');
    expect(formatNanoseconds(12_300)).toBe('12.3 µs');
    expect(formatNanoseconds(null)).toBe('—');
  });

  it('pluralizes', () => {
    expect(plural(1, 'error')).toBe('1 error');
    expect(plural(2, 'entry', 'entries')).toBe('2 entries');
  });
});
