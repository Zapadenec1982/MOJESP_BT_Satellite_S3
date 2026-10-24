// use:trap on a modal's box (m3/BottomSheet, m3/Dialog): Tab and Shift+Tab go round the box's own
// controls and never reach the page behind its scrim, and when the box goes the focus returns to
// what had it before — the band row, the button that opened it.

const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]):not([type=hidden]), '
  + 'select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

export function trap(node) {
  const before = typeof document !== 'undefined' ? document.activeElement : null;
  const inside = () => [...node.querySelectorAll(FOCUSABLE)].filter((el) => el.getClientRects().length > 0);

  function key(e) {
    if (e.key !== 'Tab') return;
    const f = inside();
    if (!f.length) {
      e.preventDefault();
      node.focus();
      return;
    }
    const first = f[0];
    const last = f[f.length - 1];
    const at = document.activeElement;
    if (e.shiftKey && (at === first || at === node || !node.contains(at))) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && (at === last || !node.contains(at))) {
      e.preventDefault();
      first.focus();
    }
  }

  node.addEventListener('keydown', key);
  return {
    destroy() {
      node.removeEventListener('keydown', key);
      if (before && typeof before.focus === 'function' && document.contains(before)) before.focus();
    },
  };
}
