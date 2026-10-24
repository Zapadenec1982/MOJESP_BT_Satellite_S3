import { apiGet, apiPost } from './api.js';

/*
 * The satellite's Bluetooth settings as the web reads and changes them (GET / POST /api/bt,
 * modules/bt/include/bt_web.h). The requests go through the same queue as the panel's
 * Bluetooth page, so a change shows on both; bt.cfg_rev in the state says when to read again.
 *
 * A pairing request is on the panel and in every open tab at once. bt.pair_* in the state says
 * one is waiting; the code itself comes only from GET /api/bt, behind the login, because the
 * WebSocket is open to the whole network. The first answer wins: a second one gets 409
 * {"error":"answered","by":1|2}.
 */

/** The optional codecs, as the panel's chips show them (screen_bt.cpp). SBC is bit 0, always on. */
export const CODECS = [
  { bit: 1 << 1, name: 'AAC' },
  { bit: 1 << 2, name: 'aptX' },
  { bit: 1 << 3, name: 'aptX HD' },
  { bit: 1 << 4, name: 'LHDC' },
];

/** bt.pair_kind (sat::PAIR_*) */
export const PAIR_NUMERIC = 0;
export const PAIR_CONSENT = 1;
export const PAIR_PASSKEY = 2;

/** bt.pair_by */
export const BY_PANEL = 1;
export const BY_WEB = 2;

/** The device name: 1..32 bytes of UTF-8 (sat::BT_NAME_MAX). */
export const NAME_MAX = 32;

export const loadBt = () => apiGet('/api/bt');

/**
 * One request to the satellite. Resolves { ok: true }, or { error, by } for an answer the
 * device turned down (409: the satellite is missing or older, the pairing was answered first
 * elsewhere); throws on anything else.
 */
export async function sendBt(body) {
  try {
    return await apiPost('/api/bt', body);
  } catch (e) {
    try {
      const r = JSON.parse(e.message);
      if (r && r.error) return r;
    } catch (x) { /* not a JSON reply */ }
    throw e;
  }
}

/** The six digits as the panel shows them: "123 456". */
export const codeText = (n) => {
  const s = String(n ?? 0).padStart(6, '0');
  return `${s.slice(0, 3)} ${s.slice(3)}`;
};
