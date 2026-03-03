/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{ts,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: {
        sans: ['Fira Sans', 'ui-sans-serif', 'system-ui', 'sans-serif'],
        mono: ['Fira Code', 'ui-monospace', 'SFMono-Regular', 'monospace'],
      },
      colors: {
        // UI accent — desert amber
        'accent': '#f59e0b',           // amber-400  — active nav, highlights
        'accent-dim': '#92400e',       // amber-900  — 10% tint backgrounds
        'accent-muted': 'rgba(245,158,11,0.12)', // subtle bg for active items

        // Severity colors (semantic — do not use for UI chrome)
        'severity-ok': '#22c55e',      // green-500
        'severity-warn': '#eab308',    // yellow-500
        'severity-crit': '#ef4444',    // red-500
        'severity-unknown': '#6b7280', // gray-500

        // Dark mode — OLED palette
        'dark-bg': '#0f172a',          // slate-900  — page background
        'dark-surface': '#0d1523',     // even darker — sidebar / header
        'dark-card': '#1a2438',        // card surface
        'dark-card-hover': '#1e293b',  // card hover
        'dark-border': '#263047',      // border
        'dark-border-subtle': '#1a2438', // barely-there border
      },
      boxShadow: {
        'card-dark': '0 1px 3px rgba(0,0,0,0.4), 0 1px 2px rgba(0,0,0,0.3)',
        'card-dark-hover': '0 4px 12px rgba(0,0,0,0.5)',
      },
      animation: {
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
      },
    },
  },
  plugins: [],
}
