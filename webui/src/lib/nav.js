import { K } from '../stores/i18n.js';

/**
 * The three sections, in the order and with the icons of the panel's drawer (ui_shell.cpp PAGES).
 * Налаштування keeps the id "system" its pages' addresses have always had (lib/routes.js).
 */
export const NAV = [
  { id: 'player', icon: 'play_circle', key: K('nav.player') },
  { id: 'source', icon: 'speaker_group', key: K('nav.source') },
  { id: 'system', icon: 'settings', key: K('nav.settings') },
];

/** The generated cards' icons were Lucide names in the manifests; drawn with the panel's glyphs. */
const LEGACY_ICONS = {
  home: 'home',
  'volume-2': 'volume_up',
  monitor: 'brightness_6',
  radio: 'radio',
  link: 'memory',
  wifi: 'wifi',
  cpu: 'developer_board',
  settings: 'settings',
  'chevron-right': 'chevron_right',
};

export function legacyIcon(name) {
  return LEGACY_ICONS[name] || 'view_list';
}
