import { describe, expect, it } from 'vitest';
import type { Domain, Severity } from '../types/health';
import { DOMAINS } from './domains';
import { verdictFor } from './verdict';

const all = (severity: Severity) => Object.fromEntries(DOMAINS.map((d) => [d, severity])) as Record<Domain, Severity>;

describe('verdictFor', () => {
  it('is calm when everything is healthy', () => {
    expect(verdictFor(all('ok'))).toEqual({ headline: 'All domains healthy', detail: null });
  });

  it('names a single domain that needs attention', () => {
    expect(verdictFor({ ...all('ok'), journal: 'warn' }).headline).toBe('Journal needs attention');
  });

  it('counts several faults and still mentions blind spots', () => {
    const v = verdictFor({ ...all('ok'), services: 'crit', update: 'warn', time_sync: 'stale' });
    expect(v.headline).toBe('2 domains need attention');
    expect(v.detail).toBe("Can't currently see time sync.");
  });

  it('does not call a partially blind gateway healthy', () => {
    const v = verdictFor({ ...all('ok'), resources: 'unavailable', time_sync: 'stale' });
    expect(v.headline).toBe("Can't see 2 domains");
  });

  it('distinguishes warm-up from health', () => {
    expect(verdictFor(all('unknown')).headline).toBe('Waiting for the first collection');
    expect(verdictFor({ ...all('ok'), crash: 'unknown' }).headline).toBe('Everything observed is healthy');
  });
});
