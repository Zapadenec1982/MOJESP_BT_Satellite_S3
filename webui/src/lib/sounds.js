// The sounds by id (components/audio_dsp/src/eq_presets.cpp) and their names in the catalogs —
// the kept presets too, since one plays under its name until another is chosen. The device says
// which are offered (GET /api/eq "presets"); the panel names them from the same catalogs
// (screen_eq.cpp PRESET_NAMES). docs/PLAN_SOUND_UX_2026-10-14.md, S2 and S3.
import { K } from '../stores/i18n.js';

export const SOUND_KEYS = {
  custom: K('eq.preset.custom'),
  flat: K('eq.preset.flat'),
  pop: K('eq.preset.pop'),
  rock: K('eq.preset.rock'),
  jazz: K('eq.preset.jazz'),
  classical: K('eq.preset.classical'),
  electronic: K('eq.preset.electronic'),
  hiphop: K('eq.preset.hiphop'),
  loudness: K('eq.preset.loudness'),
  night: K('eq.preset.night'),
  more_bass: K('eq.preset.more_bass'),
  more_treble: K('eq.preset.more_treble'),
  bass_treble: K('eq.preset.bass_treble'),
  headphones: K('eq.preset.headphones'),
  large_hall: K('eq.preset.large_hall'),
  live: K('eq.preset.live'),
  party: K('eq.preset.party'),
  club: K('eq.preset.club'),
  reggae: K('eq.preset.reggae'),
  ska: K('eq.preset.ska'),
  soft: K('eq.preset.soft'),
  soft_rock: K('eq.preset.soft_rock'),
  dance: K('eq.preset.dance'),
};

/** A sound's name in the language of `t`; its id for one this page does not know. */
export const soundName = (t, id) => (SOUND_KEYS[id] ? t[SOUND_KEYS[id]] : id);
