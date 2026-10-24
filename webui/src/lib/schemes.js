// The panel's four colour schemes (ui.theme 0..3; screen_display.cpp SCHEME_NAMES): each one's
// name in the catalogs and its swatch, the scheme's primary as in m3_schemes.css.
import { K } from '../stores/i18n.js';

export const SCHEMES = [
  { v: 0, key: K('sys.scheme_amber'), c: '#FFB95F' },
  { v: 1, key: K('sys.scheme_blue'), c: '#7A82C0' },
  { v: 2, key: K('sys.scheme_green'), c: '#9A9973' },
  { v: 3, key: K('sys.scheme_red'), c: '#C45D4A' },
];

/** The scheme `v`'s catalog key; the first scheme's for one this page does not know. */
export const schemeKey = (v) => (SCHEMES[v] || SCHEMES[0]).key;
