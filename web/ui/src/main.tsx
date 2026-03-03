import { render } from 'preact';
import { App } from './App';
import './index.css';

// Initialize theme before rendering
const savedTheme = localStorage.getItem('theme');
const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
if (savedTheme === 'dark' || (!savedTheme && prefersDark)) {
  document.documentElement.classList.add('dark');
}

render(<App />, document.getElementById('app')!);
