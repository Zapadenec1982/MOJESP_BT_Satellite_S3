import { writable, derived } from 'svelte/store';
import { createWebSocket, WS_CLOSE_BUSY } from '../lib/websocket.js';

/** All SharedState keys as reactive store */
export const state = writable({});

/** WebSocket connection status */
export const wsConnected = writable(false);

/**
 * How many times the socket has opened. A hidden tab lets go of its socket without saying so
 * (lib/websocket.js), so wsConnected stays true across hiding it; this counts every open, the
 * one after the tab is shown again included (lib/update.js looks then).
 */
export const wsOpened = writable(0);

/**
 * The panel serves three WebSocket clients (ws_service.h) and closes a fourth with WS_CLOSE_BUSY.
 * Firmware from before that close code accepted the fourth and then never sent it anything, so an
 * open socket that brings no snapshot within 3 s is still taken for "busy" as well.
 */
export const wsBusy = writable(false);

const BUSY_AFTER_MS = 3000;
let busyTimer = null;
let wsClient = null;

export function initWebSocket() {
  if (wsClient) return;
  wsClient = createWebSocket((data) => {
    if ('_ws_connected' in data) {
      wsConnected.set(data._ws_connected);
      if (data._ws_connected) wsOpened.update((n) => n + 1);
      clearTimeout(busyTimer);
      // Stays busy across the retry's open: the panel either sends the snapshot or says busy again.
      if (data._ws_connected) busyTimer = setTimeout(() => wsBusy.set(true), BUSY_AFTER_MS);
      else wsBusy.set(data._ws_code === WS_CLOSE_BUSY);
      return;
    }
    clearTimeout(busyTimer);
    wsBusy.set(false);
    state.update(s => ({ ...s, ...data }));
  });
}

/** Optimistic update: set value locally before server confirms */
export function setStateKey(key, value) {
  state.update(s => ({ ...s, [key]: value }));
}

/** Get a derived store for a single state key */
export function stateKey(key) {
  return derived(state, $s => $s[key]);
}
