import { state } from './state.js';

/*
 * The colour scheme follows the panel: ui.theme 0..3 (Бурштин, Синя, Зелена, Червона) picks the
 * block of m3_schemes.css through data-scheme on <html>. Change it on the panel and the open
 * page turns with it. The last one seen is kept so the next visit does not flash amber first.
 */

const KEY = 'p4-scheme';

function apply(n) {
  const root = document.documentElement;
  if (root.dataset.scheme === String(n)) return;
  root.dataset.scheme = String(n);
  // the phone's browser bar takes the surface colour too
  const surface = getComputedStyle(root).getPropertyValue('--surface').trim();
  let meta = document.querySelector('meta[name="theme-color"]');
  if (!meta) {
    meta = document.createElement('meta');
    meta.name = 'theme-color';
    document.head.appendChild(meta);
  }
  if (surface) meta.content = surface;
}

try {
  const cached = localStorage.getItem(KEY);
  apply(/^[0-3]$/.test(cached || '') ? cached : 0);
} catch (e) {
  apply(0);
}

state.subscribe((s) => {
  const v = s['ui.theme'];
  if (!Number.isInteger(v) || v < 0 || v > 3) return;
  apply(v);
  try { localStorage.setItem(KEY, String(v)); } catch (e) { /* storage blocked */ }
});
