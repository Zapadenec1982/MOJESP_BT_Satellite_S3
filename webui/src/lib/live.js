import { readable } from 'svelte/store';
import { apiGet } from './api.js';

/*
 * The device's whole state, read every 2 s while a page that shows diagnostics is open and the
 * tab is visible. The diagnostics — CPU, heap, the limiter, the satellite's
 * counters — are written without WebSocket deltas, and the firmware is not changed for that.
 * One poll however many components subscribe; none at all once they are gone.
 *
 * Writable settings still come from the WebSocket (stores/state.js): a poll answered before a
 * change lands would put the old value back for two seconds. So a page reads live values for
 * read-only keys and $state for the rest.
 */
export const live = readable({}, (set) => {
  const read = async () => {
    if (document.visibilityState !== 'visible') return;
    try {
      set(await apiGet('/api/state'));
    } catch (e) { /* the next tick tries again */ }
  };
  const onVisible = () => document.visibilityState === 'visible' && read();
  read();
  const timer = setInterval(read, 2000);
  document.addEventListener('visibilitychange', onVisible);
  return () => {
    clearInterval(timer);
    document.removeEventListener('visibilitychange', onVisible);
  };
});
