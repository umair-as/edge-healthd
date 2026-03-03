import type { ComponentChildren } from 'preact';
import { Header } from './Header';
import { Nav } from './Nav';

interface LayoutProps {
  children: ComponentChildren;
}

export function Layout({ children }: LayoutProps) {
  return (
    <div class="min-h-screen flex flex-col bg-gray-50 dark:bg-dark-bg text-gray-900 dark:text-slate-100">
      <Header />
      <div class="flex flex-1 overflow-hidden">
        <Nav />
        <main class="flex-1 overflow-y-auto px-4 py-6 pb-24 md:pb-6 md:px-6">
          {children}
        </main>
      </div>
    </div>
  );
}
