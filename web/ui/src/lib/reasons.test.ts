import { describe, expect, it } from 'vitest';
import { fixtures } from '../test/fixtures';
import { hasReason, parseReason, resourceFlag } from './reasons';

describe('reason codes', () => {
  it('splits code and subject at the first colon only', () => {
    expect(parseReason('mem_used_high')).toEqual({ code: 'mem_used_high', subject: null });
    expect(parseReason('disk_used_high:/data')).toEqual({ code: 'disk_used_high', subject: '/data' });
    expect(parseReason('svc_failed:a:b')).toEqual({ code: 'svc_failed', subject: 'a:b' });
  });

  it('matches by code and, optionally, subject', () => {
    const s = fixtures.critical();
    expect(hasReason(s, 'disk_used_high')).toBe(true);
    expect(hasReason(s, 'disk_used_high', '/data')).toBe(true);
    expect(hasReason(s, 'disk_used_high', '/')).toBe(false);
  });

  it("flags readings at the resources domain's level and leaves the rest neutral", () => {
    const s = fixtures.critical();
    expect(resourceFlag(s, 'temp_high', 'cpu_thermal')).toBe('crit');
    expect(resourceFlag(s, 'temp_high', 'gpu_thermal')).toBeNull();
  });
});
