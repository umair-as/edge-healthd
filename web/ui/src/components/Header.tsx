import { hostname, overallSeverity, isDarkMode, toggleDarkMode } from '../state/signals';
import { StatusBadge } from './StatusBadge';
import { ConnectionStatus } from './ConnectionStatus';

export function Header() {
  return (
    <header class="bg-white dark:bg-dark-card border-b border-gray-200 dark:border-dark-border sticky top-0 z-10">
      <div class="container mx-auto px-4 py-3">
        <div class="flex items-center justify-between">
          {/* Left: Device info */}
          <div class="flex items-center gap-3">
            <div class="flex items-center gap-2">
              <StatusBadge severity={overallSeverity.value} size="lg" pulse />
              <div>
                <h1 class="text-lg font-semibold leading-tight">{hostname.value}</h1>
                <p class="text-xs text-gray-500 dark:text-gray-400">Edge Health</p>
              </div>
            </div>
          </div>

          {/* Right: Connection + Theme toggle */}
          <div class="flex items-center gap-3">
            <ConnectionStatus />
            <button
              onClick={toggleDarkMode}
              class="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-slate-700 transition-colors"
              aria-label="Toggle dark mode"
            >
              {isDarkMode.value ? (
                <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
                </svg>
              ) : (
                <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
                </svg>
              )}
            </button>
          </div>
        </div>
      </div>
    </header>
  );
}
