/*
 * Radio Browser from the browser (api.radio-browser.info: CORS *, no key). The rules are the
 * panel's own (components/radio_catalog: keep(), search_path()) as data/www/radio.html had them:
 * MP3, AAC, AAC+ and FLAC, no HLS, a stream URL that fits the station list, the quality floor,
 * one row per stream. Ranking only within a country or a genre: global votes are inflated
 * (docs/NETWORK.md, "Radio Browser catalog"). The panel gets only a station's name, stream
 * and uuid (radio.web_*).
 */

import { URL_MAX } from './stations.js';

let base = null;

async function rb(path) {
  if (!base) {
    // The server list, not a fixed host: one is left (de1), and that may change.
    try {
      const list = await (await fetch('https://all.api.radio-browser.info/json/servers')).json();
      const names = [...new Set(list.map((s) => s.name).filter(Boolean))];
      base = 'https://' + names[Math.floor(Math.random() * names.length)];
    } catch (e) {
      base = 'https://de1.api.radio-browser.info';
    }
  }
  const r = await fetch(base + path);
  if (!r.ok) throw new Error('Radio Browser: ' + r.status);
  return r.json();
}

const PLAYABLE = new Set(['MP3', 'AAC', 'AAC+', 'FLAC']);
const ORDER = ['&order=clickcount&reverse=true', '&order=votes&reverse=true', '&order=bitrate&reverse=true', '&order=name'];
const FLOOR = ['', '&bitrateMin=128', '&bitrateMin=192', '&bitrateMin=320', '&codec=FLAC'];
const FLOOR_KBPS = [0, 128, 192, 320, 0];
export const LIMIT = 50;

function keep(s, floor) {
  const codec = (s.codec || '').toUpperCase();
  const url = s.url_resolved || s.url || '';
  if (!PLAYABLE.has(codec) || s.hls === 1 || !/^https?:\/\//.test(url) || new TextEncoder().encode(url).length > URL_MAX) return false;
  const lossless = codec === 'FLAC';
  if (floor === 0) return true;
  if (floor === 4) return lossless;
  return !lossless && s.bitrate >= FLOOR_KBPS[floor];
}

/**
 * One page of stations. spec: { country } | { tag } | { name }; uaOnly narrows a genre or a
 * search to Ukraine. Returns { rows, more }: rows already filtered and without repeats.
 */
export async function stations(spec, { sort = 0, floor = 1, uaOnly = false, offset = 0 } = {}) {
  let path = '/json/stations/search?hidebroken=true';
  if (spec.country) path += '&countrycode=' + encodeURIComponent(spec.country);
  if (spec.tag) path += '&tag=' + encodeURIComponent(spec.tag);
  if (spec.name) path += '&name=' + encodeURIComponent(spec.name);
  if ((spec.tag || spec.name) && uaOnly) path += '&countrycode=UA';
  path += ORDER[sort] + FLOOR[floor] + '&limit=' + LIMIT + '&offset=' + offset;
  const raw = await rb(path);
  const seen = new Set();
  const rows = [];
  for (const r of raw) {
    if (!keep(r, floor)) continue;
    const url = r.url_resolved || r.url;
    const key = url.split('?')[0];
    if (seen.has(key)) continue;
    seen.add(key);
    rows.push({ ...r, url, name: (r.name || '').trim() || url });
  }
  return { rows, more: raw.length >= LIMIT };
}

/** Countries with stations, Ukraine first, then by count: [{ code, n }]. */
export async function countries() {
  const list = await rb('/json/countrycodes?hidebroken=true&order=stationcount&reverse=true');
  const byCode = new Map();
  for (const c of list) {
    const code = (c.name || '').toUpperCase();
    if (!/^[A-Z]{2}$/.test(code) || !c.stationcount) continue;
    byCode.set(code, (byCode.get(code) || 0) + c.stationcount);   // "gr" and "GR" are one
  }
  return [...byCode].map(([code, n]) => ({ code, n }))
    .sort((a, b) => (b.code === 'UA') - (a.code === 'UA') || b.n - a.n);
}

/** The genre → tag table the panel reads too (data/www/radio_genres.json). */
export async function genres() {
  const r = await fetch('/radio_genres.json');
  if (!r.ok) throw new Error(String(r.status));
  return (await r.json()).genres || [];
}

/** A genre's name in `lang`: name_en, name_de, name_pl; Ukrainian, `name`, when it has none. */
export function genreName(g, lang) {
  return (lang !== 'uk' && g[`name_${lang}`]) || g.name;
}
