import { apiGet, apiPost } from './api.js';
import { K } from '../stores/i18n.js';

/*
 * The station list as the web edits it (GET / POST /api/stations, modules/radio/src/
 * stations_http.inc). The device keeps the list; the web reads it with its generation, edits a
 * copy, and sends the whole list back with that generation in front. If the list moved
 * meanwhile — a preset stored on the panel — the device answers 409 and nothing is lost.
 *
 * Limits are the device's (components/stations/include/stations.h): 64 stations, a name of
 * 63 bytes, a URL of 223 bytes, six presets holding station indices or −1.
 */

export const MAX_STATIONS = 64;
export const NAME_MAX = 63;
export const URL_MAX = 223;
export const PRESETS = 6;

const enc = new TextEncoder();
export const bytes = (s) => enc.encode(s || '').length;

/** The name cut to fit the device, at a character, never inside one (as radio.html did). */
export function fitName(name) {
  let s = (name || '').trim();
  while (bytes(s) > NAME_MAX) s = s.slice(0, -1);
  return s;
}

export const host = (url) => {
  try { return new URL(url).host; } catch (e) { return url; }
};

/** { rev, stations: [{name, url, uuid}], presets: [6 × index | −1] } */
export async function loadStations() {
  const r = await apiGet('/api/stations');
  const list = r.list || {};
  const presets = Array.from({ length: PRESETS }, (_, i) => {
    const p = (list.presets || [])[i];
    return Number.isInteger(p) && p >= 0 && p < (list.stations || []).length ? p : -1;
  });
  return { rev: r.rev, stations: list.stations || [], presets };
}

/**
 * Sends the list. Resolves 'saved' (the device took it; radio.list_rev will move), 'changed'
 * (409: the list moved on the device — reload), or throws with the reason.
 */
export async function saveStations(rev, stations, presets) {
  try {
    // rev first: the device reads it off the front of the body
    await apiPost('/api/stations', { rev, stations: stations.map(clean), presets });
    return 'saved';
  } catch (e) {
    if (/changed/.test(e.message)) return 'changed';
    throw e;
  }
}

const clean = (s) => (s.uuid ? { name: s.name, url: s.url, uuid: s.uuid } : { name: s.name, url: s.url });

// ── edits: each returns a new { stations, presets }, presets following their stations ──

export function remove(list, i) {
  const stations = list.stations.filter((_, k) => k !== i);
  const presets = list.presets.map((p) => (p === i ? -1 : p > i ? p - 1 : p));
  return { stations, presets };
}

export function move(list, i, j) {
  if (j < 0 || j >= list.stations.length || i === j) return list;
  const order = list.stations.map((_, k) => k);
  order.splice(j, 0, order.splice(i, 1)[0]);
  const where = new Map(order.map((old, k) => [old, k]));
  return {
    stations: order.map((old) => list.stations[old]),
    presets: list.presets.map((p) => (p >= 0 ? where.get(p) : -1)),
  };
}

export function rename(list, i, name) {
  const stations = list.stations.map((s, k) => (k === i ? { ...s, name: fitName(name) } : s));
  return { stations, presets: list.presets };
}

/** Station `i` into preset `slot`, or out of every preset with slot −1. */
export function setPreset(list, i, slot) {
  const presets = list.presets.map((p, k) => (k === slot ? i : slot < 0 && p === i ? -1 : p));
  return { stations: list.stations, presets };
}

/** Why a station cannot be added, as a catalog key, or null. */
export function refuse(list, name, url) {
  if (!/^https?:\/\/\S+$/i.test(url || '')) return K('st.err_url');
  if (bytes(url) > URL_MAX) return K('st.err_url_long');
  if (list.stations.some((s) => s.url === url)) return K('st.err_dup');
  if (list.stations.length >= MAX_STATIONS) return K('st.err_full');
  return null;
}

export function add(list, name, url, uuid = '') {
  const label = fitName(name) || host(url);
  return { stations: [...list.stations, { name: label, url, uuid }], presets: list.presets };
}

// ── playlists ──

/** Stations in an .m3u / .m3u8 / .pls file: [{name, url}]. */
export function parsePlaylist(text) {
  const out = [];
  const lines = String(text || '').split(/\r?\n/).map((l) => l.trim());
  if (/^\[playlist\]/i.test(lines[0] || '')) {
    const files = {};
    const titles = {};
    for (const l of lines) {
      let m = l.match(/^File(\d+)=(.+)$/i);
      if (m) files[m[1]] = m[2].trim();
      m = l.match(/^Title(\d+)=(.+)$/i);
      if (m) titles[m[1]] = m[2].trim();
    }
    for (const k of Object.keys(files)) out.push({ name: titles[k] || '', url: files[k] });
    return out;
  }
  let name = '';
  for (const l of lines) {
    if (!l) continue;
    if (/^#EXTINF:/i.test(l)) {
      name = l.replace(/^#EXTINF:[^,]*,?/i, '').trim();
    } else if (!l.startsWith('#')) {
      out.push({ name, url: l });
      name = '';
    }
  }
  return out;
}

export function toM3u(stations) {
  const lines = ['#EXTM3U'];
  for (const s of stations) lines.push(`#EXTINF:-1,${s.name}`, s.url);
  return lines.join('\n') + '\n';
}
