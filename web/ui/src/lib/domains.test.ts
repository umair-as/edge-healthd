import { describe, expect, it } from 'vitest';
import type { Domain, Severity } from '../types/health';
import { fixtures } from '../test/fixtures';
import { DOMAINS, domainSeverities, domainSeverity, domainSummary, escalatedDomains, orderByAttention } from './domains';

const allOk = (): Record<Domain, Severity> =>
  Object.fromEntries(DOMAINS.map((d) => [d, 'ok'])) as Record<Domain, Severity>;

describe('domainSeverity', () => {
  it("prefers the daemon's own per-domain roll-up", () => {
    const s = fixtures.blind();
    expect(domainSeverity(s, 'time_sync')).toBe('stale');
    expect(domainSeverity(s, 'resources')).toBe('unavailable');
  });

  it('falls back to section data for snapshots without summary.domains', () => {
    const s = fixtures.critical();
    delete s.summary.domains;
    expect(domainSeverity(s, 'services')).toBe('crit');
    expect(domainSeverity(s, 'boot')).toBe('crit'); // boot_ok is false in this fixture
    expect(domainSeverity(s, 'resources')).toBe('unknown');
  });
});

describe('orderByAttention', () => {
  it('puts the worst domains first and keeps canonical order for ties', () => {
    const sev = { ...allOk(), journal: 'warn', services: 'crit', update: 'warn', crash: 'stale' } as const;
    expect(orderByAttention(sev)).toEqual(['services', 'update', 'journal', 'crash', 'boot', 'resources', 'time_sync']);
  });
});

describe('escalatedDomains', () => {
  it('reports only domains that got worse', () => {
    const before = { ...allOk(), journal: 'warn' } as const;
    const after = { ...allOk(), journal: 'ok', services: 'crit' } as const;
    expect([...escalatedDomains(before, after)]).toEqual(['services']);
  });

  it('reports nothing on the first snapshot', () => {
    expect(escalatedDomains(null, domainSeverities(fixtures.critical())).size).toBe(0);
  });
});

describe('domainSummary', () => {
  it('names a single failing unit', () => {
    const s = fixtures.healthy();
    s.services.units[1] = { ...s.services.units[1], state: 'failed', severity: 'crit' };
    expect(domainSummary(s, 'services')).toBe(`${s.services.units[1].name} is failed`);
  });

  it('counts multiple failing units', () => {
    expect(domainSummary(fixtures.critical(), 'services')).toBe('3 of 4 units need attention');
  });

  it('surfaces unreadable resources instead of hiding them', () => {
    expect(domainSummary(fixtures.blind(), 'resources')).toMatch(/2 readings unavailable$/);
  });

  it('describes crash records and acknowledgement', () => {
    expect(domainSummary(fixtures.healthy(), 'crash')).toBe('No crash records');
    expect(domainSummary(fixtures.critical(), 'crash')).toBe('2 pstore records · not acknowledged');
    const s = fixtures.critical();
    delete s.crash;
    expect(domainSummary(s, 'crash')).toBe('Not reported');
  });
});
