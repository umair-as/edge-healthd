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
      class={`bg-white dark:bg-dark-card rounded-lg border border-gray-200 dark:border-dark-border p-4 ${
        hover ? 'card-hover' : ''
      } ${className}`}
    >
      {title && (
        <h3 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">
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
    <div class={`bg-white dark:bg-dark-card rounded-lg border border-gray-200 dark:border-dark-border p-3 ${className}`}>
      {children}
    </div>
  );
}
