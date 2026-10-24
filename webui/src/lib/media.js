/*
 * The card's library and the DLNA browser, the parts with no page in them (W6, PLAN_WEBUI §2).
 * tools/tests/test_webui_media.py holds them to the firmware: the browser's command numbers and
 * their packing (components/dlna_client: dlna_browser.h, encode()), and the panel's way of
 * turning a path into a row (modules/ui_lvgl/src/screen_library.cpp).
 */

/** dlna_client::Command. */
export const BROWSE = { discover: 1, open: 2, back: 3, next: 4, prev: 5, queue: 6, cancel: 7 };

/**
 * dlna.browse_cmd: the command, the row and the revision of the snapshot it was chosen from —
 * dlna_client::encode(). A stale revision makes the browser do nothing rather than the wrong
 * thing, which is why every command carries the one on screen.
 */
export function browseCode(command, row, revision) {
  return ((revision & 0xffff) * 4096) + ((row & 255) * 16) + (command & 15);
}

/** A file name without a short, plain extension — strip_ext() of the panel's library. */
export function trackName(rel) {
  const name = String(rel || '').split('/').pop();
  const dot = name.lastIndexOf('.');
  if (dot <= 0) return name;
  const ext = name.slice(dot + 1);
  return /^[A-Za-z0-9]{2,4}$/.test(ext) ? name.slice(0, dot) : name;
}

/** The folder of an entry under the root, '' at the top: the dim line of a row. */
export function trackFolder(rel) {
  const s = String(rel || '');
  const slash = s.lastIndexOf('/');
  return slash < 0 ? '' : s.slice(0, slash);
}

/** What player.track wants: the path ready for fopen(), as Playlist::path_at() builds it. */
export function trackPath(root, rel) {
  return `${root}/${rel}`;
}

/** The filter: every word somewhere in the path (title, album, artist folder), any case. */
export function matches(rel, query) {
  const words = String(query || '').toLocaleLowerCase().split(/\s+/).filter(Boolean);
  if (!words.length) return true;
  const hay = String(rel || '').toLocaleLowerCase();
  return words.every((w) => hay.includes(w));
}

/**
 * The path above the folder on screen, for the crumbs. The browser publishes only the folder
 * it shows (View.title), so the web keeps the names it opened on the way down and checks each new
 * snapshot against them. `crumbs` are the names under "Media servers": the server, then each
 * folder; null stands for levels this tab did not see opened (the panel went there, or the page
 * was reloaded) — one tap on it climbs one level.
 *
 * `expect` is what the last command should have done: { open: name } or { back: true }, or null.
 * Called once per settled snapshot of a new revision, so a folder named like its parent ("CD1"
 * in "CD1") is still one level more.
 */
export function nextCrumbs(crumbs, expect, view) {
  if (view.servers) return [];
  const top = crumbs[crumbs.length - 1];
  if (expect && expect.open !== undefined && view.title === expect.open) {
    return [...crumbs, expect.open];
  }
  if (expect && expect.back && crumbs.length >= 2 && crumbs[crumbs.length - 2] === view.title) {
    return crumbs.slice(0, -1);
  }
  if (top === view.title) return crumbs;
  return [null, view.title];
}
