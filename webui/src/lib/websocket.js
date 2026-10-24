/**
 * Auto-reconnecting WebSocket client.
 * Calls onMessage(data) with parsed JSON on each message.
 * Sends _ws_connected meta-events for connection status; a close also carries _ws_code.
 *
 * A hidden tab lets go of its socket. The panel serves three WebSocket clients (ws_service.h),
 * and a tab nobody is looking at should not hold one of them; it connects again, and gets a
 * fresh snapshot, the moment it is shown. Letting go sends no _ws_connected: false - nobody
 * sees the tab meanwhile, and a "connection restored" every time it comes back would be noise.
 */

/** The panel's close code for a client it has no slot for: 1013 "Try Again Later". */
export const WS_CLOSE_BUSY = 1013;
/** While the panel is busy, ask again at this steady pace rather than backing off to 30 s. */
const BUSY_RETRY_MS = 5000;

export function createWebSocket(onMessage) {
  let ws = null;
  let retry = 1000;
  let timer = null;
  let stopped = false;

  const hidden = () => document.visibilityState === 'hidden';

  function schedule(ms) {
    clearTimeout(timer);
    timer = setTimeout(connect, ms);
  }

  function connect() {
    timer = null;
    if (stopped || hidden()) return;
    if (ws && ws.readyState < 2) return;

    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const sock = new WebSocket(`${proto}//${location.host}/ws`);
    ws = sock;

    sock.onopen = () => {
      if (sock !== ws) return;
      retry = 1000;
      onMessage({ _ws_connected: true });
    };

    sock.onmessage = (e) => {
      if (sock !== ws) return;
      try {
        onMessage(JSON.parse(e.data));
      } catch (err) {
        console.warn('WS parse error', err);
      }
    };

    sock.onclose = (e) => {
      if (sock !== ws) return;  // let go of on purpose, see release()
      ws = null;
      onMessage({ _ws_connected: false, _ws_code: e.code });
      if (stopped || hidden()) return;
      if (e.code === WS_CLOSE_BUSY) {
        schedule(BUSY_RETRY_MS);
      } else {
        schedule(retry);
        retry = Math.min(retry * 2, 30000);
      }
    };

    sock.onerror = () => {};
  }

  /** Close on purpose: nothing reconnects until resume(). */
  function release() {
    clearTimeout(timer);
    timer = null;
    const sock = ws;
    ws = null;
    if (sock) sock.close();
  }

  function resume() {
    if (stopped || hidden() || ws) return;
    retry = 1000;
    clearTimeout(timer);
    connect();
  }

  function onVisibility() {
    if (hidden()) release();
    else resume();
  }

  document.addEventListener('visibilitychange', onVisibility);
  window.addEventListener('pagehide', release);
  window.addEventListener('pageshow', resume);
  connect();

  return {
    stop() {
      stopped = true;
      document.removeEventListener('visibilitychange', onVisibility);
      window.removeEventListener('pagehide', release);
      window.removeEventListener('pageshow', resume);
      release();
    }
  };
}
