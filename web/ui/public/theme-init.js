// Apply the saved theme (or the OS preference) before first paint, so the page
// never flashes the wrong theme. Kept external: the server's CSP disallows
// inline scripts. Mirrors src/state/theme.ts.
(function () {
  var pref = null;
  try { pref = localStorage.getItem('theme'); } catch { /* storage disabled */ }
  var dark = pref === 'dark' || (pref !== 'light' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.classList.toggle('dark', dark);
})();
