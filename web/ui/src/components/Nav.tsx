import { route } from 'preact-router';

interface NavItem {
  path: string;
  label: string;
  icon: preact.JSX.Element;
}

const navItems: NavItem[] = [
  {
    path: '/',
    label: 'Dashboard',
    icon: (
      <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.75">
        <path stroke-linecap="round" stroke-linejoin="round" d="M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6" />
      </svg>
    ),
  },
  {
    path: '/services',
    label: 'Services',
    icon: (
      <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.75">
        <path stroke-linecap="round" stroke-linejoin="round" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" />
        <path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
      </svg>
    ),
  },
  {
    path: '/network',
    label: 'Network',
    icon: (
      <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.75">
        <path stroke-linecap="round" stroke-linejoin="round" d="M8.111 16.404a5.5 5.5 0 017.778 0M12 20h.01m-7.08-7.071c3.904-3.905 10.236-3.905 14.141 0M1.394 9.393c5.857-5.857 15.355-5.857 21.213 0" />
      </svg>
    ),
  },
  {
    path: '/resources',
    label: 'Resources',
    icon: (
      <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.75">
        <path stroke-linecap="round" stroke-linejoin="round" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
      </svg>
    ),
  },
  {
    path: '/time',
    label: 'Time',
    icon: (
      <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.75">
        <path stroke-linecap="round" stroke-linejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
      </svg>
    ),
  },
  {
    path: '/update',
    label: 'Update',
    icon: (
      <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.75">
        <path stroke-linecap="round" stroke-linejoin="round" d="M4 16v1a3 3 0 003 3h10a3 3 0 003-3v-1m-4-4l-4 4m0 0l-4-4m4 4V4" />
      </svg>
    ),
  },
];

function NavButton({ item, isActive, onClick }: { item: NavItem; isActive: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      aria-label={item.label}
      class={`flex items-center gap-3 w-full px-4 py-2.5 rounded-lg text-sm font-medium transition-colors duration-150 cursor-pointer ${
        isActive
          ? 'bg-accent-muted text-accent border-l-2 border-accent pl-[14px]'
          : 'text-gray-500 dark:text-slate-400 hover:text-gray-900 dark:hover:text-slate-100 hover:bg-gray-100 dark:hover:bg-white/5 border-l-2 border-transparent pl-[14px]'
      }`}
    >
      {item.icon}
      <span>{item.label}</span>
    </button>
  );
}

export function Nav() {
  const currentPath = typeof window !== 'undefined' ? window.location.pathname : '/';

  return (
    <>
      {/* Desktop sidebar */}
      <nav class="hidden md:flex flex-col w-52 shrink-0 bg-gray-100 dark:bg-dark-surface border-r border-gray-200 dark:border-dark-border py-3 px-2 gap-0.5">
        {navItems.map((item) => (
          <NavButton
            key={item.path}
            item={item}
            isActive={currentPath === item.path}
            onClick={() => route(item.path)}
          />
        ))}
      </nav>

      {/* Mobile bottom bar */}
      <nav class="md:hidden fixed bottom-0 left-0 right-0 z-20 bg-white dark:bg-dark-surface border-t border-gray-200 dark:border-dark-border flex">
        {navItems.map((item) => {
          const isActive = currentPath === item.path;
          return (
            <button
              key={item.path}
              onClick={() => route(item.path)}
              aria-label={item.label}
              class={`flex flex-col items-center justify-center flex-1 py-2 gap-1 text-xs cursor-pointer transition-colors duration-150 ${
                isActive ? 'text-accent' : 'text-gray-400 dark:text-slate-400'
              }`}
            >
              {item.icon}
              <span>{item.label}</span>
            </button>
          );
        })}
      </nav>
    </>
  );
}
