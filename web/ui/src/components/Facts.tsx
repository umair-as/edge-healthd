import type { ComponentChildren } from 'preact';

export interface Fact {
  label: string;
  value: ComponentChildren;
  mono?: boolean;
}

// Label/value pairs as a real description list. Empty values are skipped, so
// callers can pass optional fields without guarding each one.
export function Facts({ facts, class: className = '' }: { facts: Fact[]; class?: string }) {
  const shown = facts.filter((f) => f.value !== null && f.value !== undefined && f.value !== '');
  if (shown.length === 0) return null;

  return (
    <dl class={`grid grid-cols-[max-content_1fr] gap-x-6 gap-y-1.5 text-sm ${className}`}>
      {shown.map((f) => (
        <div key={f.label} class="contents">
          <dt class="text-ink-muted">{f.label}</dt>
          <dd class={`min-w-0 break-words ${f.mono ? 'font-mono text-[0.8125rem]' : ''}`}>{f.value}</dd>
        </div>
      ))}
    </dl>
  );
}

export function SubHeading({ children }: { children: ComponentChildren }) {
  return <h4 class="eyebrow mb-2">{children}</h4>;
}
