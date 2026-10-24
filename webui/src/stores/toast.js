import { writable } from 'svelte/store';
import { createUndo, UNDO_MS } from '../lib/undo.js';

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
export function toastError(msg) { addToast(msg, 'error', 8000); }
export function toastWarn(msg) { addToast(msg, 'warn', 5000); }

/**
 * A snackbar with an action — "Повернути" after a reset (docs/PLAN_SOUND_UX_2026-10-14.md, S3).
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
