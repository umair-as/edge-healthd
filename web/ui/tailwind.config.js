/** @type {import('tailwindcss').Config} */

// Colors are CSS variables (see src/index.css) so one set of utility classes
// serves both themes. Values are space-separated RGB channels, which keeps
// Tailwind's opacity modifiers (e.g. bg-warn/15) working.
const token = (name) => `rgb(var(--${name}) / <alpha-value>)`;

export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: {
        // System stacks only: no external font requests (the server's CSP
        // allows fonts from 'self' only, and gateways are often offline).
        sans: ['system-ui', '-apple-system', 'Segoe UI', 'Roboto', 'Ubuntu', 'Cantarell', 'Noto Sans', 'sans-serif'],
        mono: ['ui-monospace', 'SF Mono', 'Cascadia Code', 'JetBrains Mono', 'Menlo', 'Consolas', 'Liberation Mono', 'monospace'],
      },
      colors: {
        // Surfaces and ink
        bg: token('bg'),
        panel: token('panel'),
        raised: token('raised'),
        line: token('line'),
        ink: token('ink'),
        'ink-muted': token('ink-muted'),
        'ink-faint': token('ink-faint'),
        focus: token('focus'),

        // Severity (semantic only — never used for UI chrome). Amber and red are
        // reserved for warn/crit, so interactive chrome uses `focus`, not amber.
        ok: token('ok'),
        warn: token('warn'),
        crit: token('crit'),
        unknown: token('unknown'),
        // Loss-of-observability states, deliberately off the ok→crit hue ramp
        stale: token('stale'),
        unavailable: token('unavailable'),
      },
      fontSize: {
        eyebrow: ['0.6875rem', { lineHeight: '1rem', letterSpacing: '0.08em' }],
      },
    },
  },
  plugins: [],
};
