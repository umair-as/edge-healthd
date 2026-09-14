import { cleanup } from '@testing-library/preact';
import { afterEach, beforeEach } from 'vitest';
import { resetStore } from '../state/store';

// jsdom has no matchMedia; the theme and reduced-motion checks need one.
if (!window.matchMedia) {
  window.matchMedia = (query: string) =>
    ({
      matches: false,
      media: query,
      onchange: null,
      addEventListener: () => {},
      removeEventListener: () => {},
      addListener: () => {},
      removeListener: () => {},
      dispatchEvent: () => false,
    }) as MediaQueryList;
}

if (!Element.prototype.scrollIntoView) {
  Element.prototype.scrollIntoView = () => {};
}

beforeEach(() => {
  localStorage.clear();
  window.history.replaceState(null, '', '/');
  resetStore();
});

afterEach(() => {
  cleanup();
});
