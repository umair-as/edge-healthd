/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{ts,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        // Severity colors
        'severity-ok': '#22c55e',      // green-500
        'severity-warn': '#eab308',    // yellow-500
        'severity-crit': '#ef4444',    // red-500
        'severity-unknown': '#6b7280', // gray-500

        // Dark mode background colors
        'dark-bg': '#0f172a',          // slate-900
        'dark-card': '#1e293b',        // slate-800
        'dark-border': '#334155',      // slate-700
      },
      animation: {
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
      },
    },
  },
  plugins: [],
}
