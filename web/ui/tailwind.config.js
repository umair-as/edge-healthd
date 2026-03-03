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

        // Dark mode — neutral grey palette
        'dark-bg': '#141414',          // page background — near-black
        'dark-surface': '#1e1e1e',     // sidebar / header — VS Code grey
        'dark-card': '#242424',        // card surface
        'dark-card-hover': '#2c2c2c',  // card hover
        'dark-border': '#363636',      // border
        'dark-border-subtle': '#2a2a2a', // barely-there border
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
