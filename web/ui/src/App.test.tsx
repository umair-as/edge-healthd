import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { describe, expect, it, vi } from 'vitest';
import { fixtures } from './test/fixtures';
import { SNAPSHOT_SILENCE_SECONDS } from './lib/clock';
import { acceptSnapshot, firstSeenAt, now, problem } from './state/store';

// The transport is tested in state/feed.test.ts; here the page is driven by
// putting snapshots straight into the store.
vi.mock('./state/feed', () => ({
  startHealthFeed: () => () => {},
  requestCollection: vi.fn(async () => 'rate-limited'),
}));

const { App } = await import('./App');

const sectionToggle = (name: RegExp) => screen.getByRole('button', { name });

describe('App', () => {
  it('tells the operator what needs attention, problems first', () => {
    acceptSnapshot(fixtures.critical(), 'network');
    render(<App />);

    expect(screen.getByRole('heading', { level: 2, name: '6 domains need attention' })).toBeTruthy();

    const tiles = within(screen.getByRole('navigation', { name: 'Domains' })).getAllByRole('button');
    expect(tiles.map((t) => t.getAttribute('aria-label'))).toEqual([
      'Boot: OK. Show details',
      'Services: Critical. Show details',
      'Resources: Critical. Show details',
      'Time sync: Warning. Show details',
      'Update: Critical. Show details',
      'Journal: Critical. Show details',
      'Crash: Critical. Show details',
    ]);

    // Faulty domains start expanded, healthy ones collapsed.
    expect(sectionToggle(/^Services\s?, Critical/).getAttribute('aria-expanded')).toBe('true');
    expect(sectionToggle(/^Boot\s?, OK/).getAttribute('aria-expanded')).toBe('false');
  });

  it('shows unreadable readings explicitly instead of blanks or zeros', () => {
    acceptSnapshot(fixtures.critical(), 'network');
    render(<App />);
    expect(screen.getByText('Mount could not be read')).toBeTruthy();
    expect(document.body.textContent).not.toMatch(/undefined|NaN/);
  });

  it('calls out loss of visibility and stale sections', () => {
    acceptSnapshot(fixtures.blind(), 'network');
    render(<App />);
    expect(screen.getByRole('heading', { level: 2, name: "Can't see 2 domains" })).toBeTruthy();
    expect(screen.getByText('Sensor could not be read')).toBeTruthy();
    expect(within(sectionToggle(/^Time sync\s?, Stale/)).getByText(/^stale/)).toBeTruthy();
  });

  it('keeps a healthy gateway quiet: everything collapsed', () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    render(<App />);
    expect(screen.getByRole('heading', { level: 2, name: 'All domains healthy' })).toBeTruthy();
    const toggles = screen.getAllByRole('button').filter((b) => b.hasAttribute('aria-expanded'));
    expect(toggles).toHaveLength(7);
    expect(toggles.every((b) => b.getAttribute('aria-expanded') === 'false')).toBe(true);
  });

  it('expands and collapses a section', () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    render(<App />);
    const toggle = sectionToggle(/^Boot\s?, OK/);
    const panel = document.getElementById(toggle.getAttribute('aria-controls')!)!;
    expect(panel.hidden).toBe(true);
    fireEvent.click(toggle);
    expect(toggle.getAttribute('aria-expanded')).toBe('true');
    expect(panel.hidden).toBe(false);
    fireEvent.click(toggle);
    expect(panel.hidden).toBe(true);
  });

  it('opens a domain from its annunciator tile and records it in the URL', async () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    render(<App />);
    fireEvent.click(screen.getByRole('button', { name: 'Journal: OK. Show details' }));
    await waitFor(() => expect(sectionToggle(/^Journal\s?, OK/).getAttribute('aria-expanded')).toBe('true'));
    expect(window.location.hash).toBe('#journal');
  });

  it('keeps links from the previous multi-page UI working', async () => {
    window.history.replaceState(null, '', '/network');
    acceptSnapshot(fixtures.healthy(), 'network');
    render(<App />);
    await waitFor(() => expect(sectionToggle(/^Resources\s?, OK/).getAttribute('aria-expanded')).toBe('true'));
    expect(window.location.pathname + window.location.hash).toBe('/#resources');
  });

  it('explains an empty state file', () => {
    problem.value = { kind: 'no-snapshot' };
    render(<App />);
    expect(screen.getByRole('heading', { name: 'No snapshot yet' })).toBeTruthy();
  });

  it('keeps showing the last good snapshot when an update is rejected', () => {
    acceptSnapshot(fixtures.degraded(), 'network');
    acceptSnapshot({ schema: 'edge.health.state', schema_version: '2.0' }, 'network');
    render(<App />);
    expect(screen.getByRole('alert').textContent).toMatch(/latest update was rejected.*2\.0 is not supported/i);
    expect(screen.getByRole('heading', { level: 2, name: '3 domains need attention' })).toBeTruthy();
  });

  it('warns when the gateway stops producing new snapshots', () => {
    acceptSnapshot(fixtures.healthy(), 'network', 0);
    now.value = (SNAPSHOT_SILENCE_SECONDS + 60) * 1000;
    render(<App />);
    expect(screen.getByRole('alert').textContent).toMatch(/No new snapshot for 4 min/);
  });

  it('marks a snapshot restored from the browser cache', () => {
    acceptSnapshot(fixtures.degraded(), 'cache', Date.now() - 3_600_000);
    render(<App />);
    expect(screen.getByRole('alert').textContent).toMatch(/Can't reach the gateway.*saved 1 h ago/);
  });

  it('keeps the last snapshot on screen when contact is lost', () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    problem.value = { kind: 'unreachable' };
    render(<App />);
    expect(screen.getByRole('alert').textContent).toMatch(/Lost contact with the gateway/);
    expect(firstSeenAt.value).not.toBeNull();
  });

  it('reports the outcome of a collection request', async () => {
    acceptSnapshot(fixtures.healthy(), 'network');
    render(<App />);
    fireEvent.click(screen.getByRole('button', { name: 'Collect now' }));
    await waitFor(() => expect(screen.getByText(/A collection just ran/)).toBeTruthy());
  });
});
