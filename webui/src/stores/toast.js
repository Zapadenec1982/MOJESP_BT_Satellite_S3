import { writable } from 'svelte/store';
import { createUndo, UNDO_MS } from '../lib/undo.js';

/** A failure in words (lib/errors.js through the catalog). stores/i18n.js gives it — this store
 *  imports no catalog, so the pages' libraries stay loadable in Node; until then, as it came. */
let words = (raw) => (raw instanceof Error ? raw.message : raw);
export function setErrorWords(fn) { words = fn; }

export const toasts = writable([]);

const MAX_TOASTS = 3;
let nextId = 0;

function addToast(msg, type, duration, action = null) {
  const id = ++nextId;
  toasts.update(t => {
    const list = [...t, { id, msg, type, action }];
    // Max 3 тости — старіші видаляються
    return list.length > MAX_TOASTS ? list.slice(list.length - MAX_TOASTS) : list;
  });
  setTimeout(() => {
    toasts.update(t => t.filter(x => x.id !== id));
  }, duration);
  return id;
}

export function dismissToast(id) {
  toasts.update(t => t.filter(x => x.id !== id));
}

export function toastSuccess(msg) { addToast(msg, 'success', 3000); }
/** A failure: in words, whatever came with it — an Error's message or a page's text. */
export function toastError(msg) { addToast(words(msg), 'error', 8000); }
export function toastWarn(msg) { addToast(msg, 'warn', 5000); }

/**
 * A snackbar with an action — "Повернути" after a reset.
 * `run` is called once if the action is taken; the snackbar goes either way.
 */
export function toastAction(msg, label, run, duration = 6000) {
  return addToast(msg, 'action', duration, { label, run });
}

const undos = createUndo({
  show: (msg, label, run) => addToast(msg, 'action', UNDO_MS, { label, run }),
  hide: dismissToast,
});

/**
 * «Скасувати» for a setting a slider or a drag moved (lib/undo.js): { key, from, to, text, action,
 * revert } — `text(from, to)` the snackbar's words, `action` its button's, `revert(from)` puts the
 * setting back. A setting moved again while its snackbar shows keeps where it started.
 */
export const noteUndo = (change) => undos.note(change);
