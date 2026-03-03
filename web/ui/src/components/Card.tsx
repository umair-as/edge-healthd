import type { ComponentChildren } from 'preact';

interface CardProps {
  title?: string;
  children: ComponentChildren;
  className?: string;
  hover?: boolean;
}

export function Card({ title, children, className = '', hover = false }: CardProps) {
  return (
    <div
      class={`bg-white dark:bg-dark-card rounded-xl border border-gray-200 dark:border-dark-border p-4 shadow-sm dark:shadow-card-dark ${
        hover ? 'card-hover cursor-pointer' : ''
      } ${className}`}
    >
      {title && (
        <h3 class="text-sm font-semibold text-gray-600 dark:text-slate-300 uppercase tracking-wider mb-3">
          {title}
        </h3>
      )}
      {children}
    </div>
  );
}

// Compact card variant
export function CompactCard({ children, className = '' }: Omit<CardProps, 'title' | 'hover'>) {
  return (
    <div class={`bg-white dark:bg-dark-card rounded-xl border border-gray-200 dark:border-dark-border p-3 shadow-sm dark:shadow-card-dark ${className}`}>
      {children}
    </div>
  );
}
