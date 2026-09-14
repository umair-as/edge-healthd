import { useCallback, useEffect, useState } from 'preact/hooks';
import type { Domain, HealthState } from './types/health';
import { domainSection, domainSummary, isDomain, orderByAttention } from './lib/domains';
import { needsAttention } from './lib/severity';
import { startHealthFeed } from './state/feed';
import { escalated, overall, problem, severities, snapshot, startClock } from './state/store';
import { applyTheme, watchSystemTheme } from './state/theme';
import { Annunciator } from './components/Annunciator';
import { DeviceFooter } from './components/DeviceFooter';
import { DomainSection } from './components/DomainSection';
import { Notices } from './components/Notices';
import { StatusScreen } from './components/StatusScreen';
import { TopBar } from './components/TopBar';
import { Verdict } from './components/Verdict';
import { BootDetail } from './components/domains/BootDetail';
import { CrashDetail } from './components/domains/CrashDetail';
import { JournalDetail } from './components/domains/JournalDetail';
import { ResourcesDetail } from './components/domains/ResourcesDetail';
import { ServicesDetail } from './components/domains/ServicesDetail';
import { TimeSyncDetail } from './components/domains/TimeSyncDetail';
import { UpdateDetail } from './components/domains/UpdateDetail';

// Paths from the previous multi-view UI, kept working as deep links.
const LEGACY_PATHS: Record<string, Domain> = {
  '/services': 'services',
  '/network': 'resources',
  '/resources': 'resources',
  '/time': 'time_sync',
  '/update': 'update',
  '/journal': 'journal',
};

type OpenOverrides = Partial<Record<Domain, boolean>>;

export function App() {
  useEffect(() => {
    applyTheme();
    const stops = [startHealthFeed(), startClock(), watchSystemTheme()];
    return () => stops.forEach((stop) => stop());
  }, []);

  const state = snapshot.value;
  const sev = severities.value;

  // Sections needing attention start open; the user's own toggles win.
  const [overrides, setOverrides] = useState<OpenOverrides>({});
  const [focusTarget, setFocusTarget] = useState<Domain | null>(() => domainFromLocation());

  const reveal = useCallback((domain: Domain) => {
    setOverrides((o) => ({ ...o, [domain]: true }));
    setFocusTarget(domain);
    history.replaceState(null, '', `/#${domain}`);
  }, []);

  useEffect(() => {
    const onHash = () => {
      const d = domainFromLocation();
      if (d) reveal(d);
    };
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, [reveal]);

  // Scroll to and focus a revealed section once it has rendered.
  useEffect(() => {
    if (!focusTarget || !state) return;
    setOverrides((o) => (o[focusTarget] ? o : { ...o, [focusTarget]: true }));
    const frame = requestAnimationFrame(() => {
      const section = document.getElementById(`domain-${focusTarget}`);
      const reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
      section?.scrollIntoView({ block: 'start', behavior: reduce ? 'auto' : 'smooth' });
      section?.querySelector<HTMLButtonElement>('h3 button')?.focus({ preventScroll: true });
      setFocusTarget(null);
    });
    return () => cancelAnimationFrame(frame);
  }, [focusTarget, state]);

  return (
    <div class="flex min-h-screen flex-col">
      <a
        href="#domains"
        class="sr-only focus:not-sr-only focus:absolute focus:left-4 focus:top-4 focus:z-10 focus:rounded-md focus:bg-panel focus:px-3 focus:py-2"
      >
        Skip to domain details
      </a>

      <TopBar state={state} />

      <main class="mx-auto w-full max-w-5xl flex-1 px-4 md:px-6">
        <Notices />

        {state && sev ? (
          <>
            <Verdict state={state} overall={overall.value} severities={sev} />
            <Annunciator severities={sev} escalated={escalated.value} onSelect={reveal} />

            <h2 id="domains" class="sr-only" tabIndex={-1}>
              Domain details
            </h2>
            <div class="mt-6 divide-y divide-line overflow-hidden rounded-lg border border-line bg-panel">
              {orderByAttention(sev).map((domain) => (
                <DomainSection
                  key={domain}
                  domain={domain}
                  severity={sev[domain]}
                  summary={domainSummary(state, domain)}
                  freshness={domainSection(state, domain)}
                  open={overrides[domain] ?? needsAttention(sev[domain])}
                  onToggle={() =>
                    setOverrides((o) => ({ ...o, [domain]: !(o[domain] ?? needsAttention(sev[domain])) }))
                  }
                >
                  <DomainDetail state={state} domain={domain} />
                </DomainSection>
              ))}
            </div>
          </>
        ) : (
          <StatusScreen problem={problem.value} />
        )}
      </main>

      <DeviceFooter state={state} />
    </div>
  );
}

function DomainDetail({ state, domain }: { state: HealthState; domain: Domain }) {
  switch (domain) {
    case 'boot':
      return <BootDetail boot={state.boot} />;
    case 'services':
      return <ServicesDetail units={state.services.units} />;
    case 'resources':
      return <ResourcesDetail state={state} />;
    case 'time_sync':
      return <TimeSyncDetail timeSync={state.time_sync} />;
    case 'update':
      return <UpdateDetail update={state.update} />;
    case 'journal':
      return <JournalDetail journal={state.journal} />;
    case 'crash':
      return <CrashDetail crash={state.crash} />;
  }
}

function domainFromLocation(): Domain | null {
  const legacy = LEGACY_PATHS[window.location.pathname];
  if (legacy) {
    history.replaceState(null, '', `/#${legacy}`);
    return legacy;
  }
  const hash = window.location.hash.slice(1);
  return isDomain(hash) ? hash : null;
}

