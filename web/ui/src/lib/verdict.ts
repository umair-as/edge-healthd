import type { Domain, Severity } from '../types/health';
import { DOMAINS, DOMAIN_LABEL } from './domains';
import { isFault, isVisibilityLoss } from './severity';

export interface Verdict {
  headline: string;
  detail: string | null;
}

// The one sentence at the top of the page. It names what needs attention
// instead of restating the severity word.
export function verdictFor(severities: Record<Domain, Severity>): Verdict {
  const faults = DOMAINS.filter((d) => isFault(severities[d]));
  const blind = DOMAINS.filter((d) => isVisibilityLoss(severities[d]));
  const unobserved = DOMAINS.filter((d) => severities[d] === 'unknown');

  if (unobserved.length === DOMAINS.length) {
    return { headline: 'Waiting for the first collection', detail: 'No domain has been observed yet.' };
  }

  const blindNote = blind.length > 0 ? `Can't currently see ${listDomains(blind)}.` : null;

  if (faults.length > 0) {
    const headline =
      faults.length === 1 ? `${DOMAIN_LABEL[faults[0]]} needs attention` : `${faults.length} domains need attention`;
    return { headline, detail: blindNote };
  }

  if (blind.length > 0) {
    return {
      headline: blind.length === 1 ? `Can't see ${DOMAIN_LABEL[blind[0]].toLowerCase()}` : `Can't see ${blind.length} domains`,
      detail: 'Everything that could be read is healthy.',
    };
  }

  if (unobserved.length > 0) {
    return {
      headline: 'Everything observed is healthy',
      detail: `${listDomains(unobserved)} not observed yet.`,
    };
  }

  return { headline: 'All domains healthy', detail: null };
}

function listDomains(domains: Domain[]): string {
  const names = domains.map((d) => DOMAIN_LABEL[d].toLowerCase());
  if (names.length <= 2) return names.join(' and ');
  return `${names.slice(0, -1).join(', ')}, and ${names[names.length - 1]}`;
}
