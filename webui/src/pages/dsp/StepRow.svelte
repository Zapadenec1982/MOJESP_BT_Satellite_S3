<script>
  // One number of a band in its sheet: the name, the
  // value, − and + — the steps are the quick way — and the value itself a field, the exact way:
  // "1.2k" and a decimal comma are read too (as dsp/NumField does on the calibration page).
  import { createEventDispatcher } from 'svelte';
  import { parseNumber } from '../../lib/numbers.js';
  import IconButton from '../../m3/IconButton.svelte';

  export let label = '';
  /** What the field shows while it is not being typed in. */
  export let text = '';
  /** What typing starts from — the bare number. */
  export let raw = '';
  export let decLabel;
  export let incLabel;
  export let disabled = false;
  /** Values on both sides of zero (a gain): a keyboard with a minus — a phone's decimal pad
   *  (iOS) has none, so "−3" could not be typed and "3" was +3. */
  export let signed = false;

  const dispatch = createEventDispatcher();
  const id = `step-${Math.random().toString(36).slice(2, 8)}`;
  let typing = false;
  let draft = '';

  const parse = parseNumber;
  function focus() {
    typing = true;
    draft = String(raw);
  }
  function commit() {
    if (!typing) return;
    typing = false;
    const v = parse(draft);
    if (Number.isFinite(v)) dispatch('set', v);
  }
  function key(e) {
    if (e.key === 'Enter') e.target.blur();
    // Esc ends the typing, not the sheet the field is in
    if (e.key === 'Escape') { e.stopPropagation(); typing = false; e.target.blur(); }
  }
</script>

<div class="row" class:disabled>
  <label class="lbl" for={id}>{label}</label>
  <input {id} type="text" inputmode={signed ? 'text' : 'decimal'} {disabled} value={typing ? draft : text}
         on:focus={focus} on:input={(e) => (draft = e.target.value)} on:blur={commit} on:keydown={key} />
  <IconButton icon="remove" label={decLabel} {disabled} on:click={() => dispatch('dec')} />
  <IconButton icon="add" label={incLabel} {disabled} on:click={() => dispatch('inc')} />
</div>

<style>
  .row {
    display: flex;
    align-items: center;
    gap: 6px;
    min-height: 60px;
    border-bottom: 1px solid var(--outline-var);
  }
  .lbl { flex: 1; min-width: 0; font-size: 16px; }
  input {
    width: 104px;
    border: none;
    background: none;
    color: var(--on-surface);
    font: inherit;
    font-size: 17px;
    text-align: right;
    font-variant-numeric: tabular-nums;
    padding: 4px 6px;
    border-radius: 6px;
  }
  input:focus { outline: 2px solid var(--primary); outline-offset: 1px; }
  .row :global(button) { background: var(--surface-highest); }
  .disabled .lbl, .disabled input { opacity: 0.45; }
</style>
