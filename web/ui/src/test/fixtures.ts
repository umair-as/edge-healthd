import type { HealthState } from '../types/health';
import blindJson from '../../../mock/sample_states/blind.json';
import criticalJson from '../../../mock/sample_states/critical.json';
import degradedJson from '../../../mock/sample_states/degraded.json';
import healthyJson from '../../../mock/sample_states/healthy.json';

// The same schema-validated fixtures the mock server and CI use, so tests
// exercise real v1.1 shapes rather than hand-written partial objects.
// Each call returns a fresh deep copy that tests may mutate.
const clone = (value: unknown) => structuredClone(value) as HealthState;

export const fixtures = {
  healthy: () => clone(healthyJson),
  degraded: () => clone(degradedJson),
  critical: () => clone(criticalJson),
  blind: () => clone(blindJson),
};
