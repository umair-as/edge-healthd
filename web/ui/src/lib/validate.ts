import type { HealthState } from '../types/health';

// A deliberately small structural check at the network boundary. It verifies
// the fields this UI dereferences without guarding, so a truncated file, a
// wrong endpoint, or a future major schema shows a clear message instead of
// a blank page. Full schema validation stays with scripts/validate_schema.py.

export type ParseResult =
  | { ok: true; state: HealthState }
  | { ok: false; error: string };

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);

export const SUPPORTED_SCHEMA_MAJOR = 1;

export function parseHealthState(input: unknown): ParseResult {
  if (!isObj(input)) return fail('the response is not a JSON object');
  if (input.schema !== 'edge.health.state') {
    return fail(`expected schema "edge.health.state", got ${JSON.stringify(input.schema ?? null)}`);
  }

  const version = typeof input.schema_version === 'string' ? input.schema_version : '';
  const major = Number(version.split('.')[0]);
  if (major !== SUPPORTED_SCHEMA_MAJOR) {
    return fail(`schema version ${version || 'missing'} is not supported; this page reads ${SUPPORTED_SCHEMA_MAJOR}.x`);
  }

  if (typeof input.generated_at !== 'string') return fail('generated_at is missing');

  const sections = ['device', 'boot', 'services', 'resources', 'time_sync', 'update', 'journal', 'summary'];
  for (const key of sections) {
    if (!isObj(input[key])) return fail(`section "${key}" is missing`);
  }

  const services = input.services as Obj;
  if (!Array.isArray(services.units)) return fail('services.units is not a list');

  const resources = input.resources as Obj;
  if (!isObj(resources.cpu) || typeof resources.cpu.load1 !== 'number') return fail('resources.cpu is incomplete');
  if (!isObj(resources.memory) || typeof resources.memory.mem_total_mb !== 'number') {
    return fail('resources.memory is incomplete');
  }
  if (!Array.isArray(resources.network)) return fail('resources.network is not a list');
  for (const key of ['storage', 'thermal'] as const) {
    if (resources[key] !== undefined && !Array.isArray(resources[key])) return fail(`resources.${key} is not a list`);
  }

  const journal = input.journal as Obj;
  if (!Array.isArray(journal.recent_errors)) return fail('journal.recent_errors is not a list');

  const summary = input.summary as Obj;
  if (typeof summary.severity !== 'string') return fail('summary.severity is missing');
  if (!Array.isArray(summary.reasons)) return fail('summary.reasons is not a list');

  if (input.crash !== undefined && (!isObj(input.crash) || !Array.isArray(input.crash.artifacts))) {
    return fail('crash section is malformed');
  }

  return { ok: true, state: input as unknown as HealthState };
}

function fail(error: string): ParseResult {
  return { ok: false, error };
}
