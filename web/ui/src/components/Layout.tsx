import type { ComponentChildren } from 'preact';
import { Header } from './Header';
import { Nav } from './Nav';

interface LayoutProps {
  children: ComponentChildren;
}

export function Layout({ children }: LayoutProps) {
  return (
    <div class="min-h-screen flex flex-col">
      <Header />
      <main class="flex-1 container mx-auto px-4 py-6 pb-20 md:pb-6">
        {children}
      </main>
      <Nav />
    </div>
  );
}
