import { describe, expect, it } from 'vitest';
import { fixtures } from '../test/fixtures';
import { parseHealthState } from './validate';

describe('parseHealthState', () => {
  it.each(Object.keys(fixtures) as (keyof typeof fixtures)[])('accepts the %s fixture', (name) => {
    expect(parseHealthState(fixtures[name]()).ok).toBe(true);
  });

  it('accepts a v1.0 snapshot (same major version)', () => {
    const s = fixtures.healthy() as unknown as Record<string, unknown>;
    s.schema_version = '1.0';
    expect(parseHealthState(s).ok).toBe(true);
  });

  it.each<[string, (s: Record<string, unknown>) => void, RegExp]>([
    ['a future major version', (s) => (s.schema_version = '2.0'), /2\.0 is not supported/],
    ['a different document', (s) => (s.schema = 'something.else'), /expected schema/],
    ['a missing section', (s) => delete s.journal, /section "journal" is missing/],
    ['units that are not a list', (s) => ((s.services as Record<string, unknown>).units = {}), /services\.units/],
    ['incomplete cpu data', (s) => delete (s.resources as Record<string, unknown>).cpu, /resources\.cpu/],
    ['a malformed crash section', (s) => (s.crash = { present: true }), /crash section/],
  ])('rejects %s with a readable reason', (_name, mutate, message) => {
    const s = fixtures.healthy() as unknown as Record<string, unknown>;
    mutate(s);
    const result = parseHealthState(s);
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toMatch(message);
  });

  it('rejects non-objects', () => {
    expect(parseHealthState(null).ok).toBe(false);
    expect(parseHealthState([]).ok).toBe(false);
    expect(parseHealthState('state').ok).toBe(false);
  });
});
