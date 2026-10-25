<script>
  // One band parameter as on the panel's band panel — − and + at the sides of the value
  // (m3/Stepper) — and the value itself a field: on a phone the steps are the quick way, and a
  // typed number is the exact one. "1.2k" and a decimal comma are read too.
  import { createEventDispatcher } from 'svelte';
  import { parseNumber, parseNonPositive } from '../../lib/numbers.js';
  import IconButton from '../../m3/IconButton.svelte';

  export let caption = '';
  /** What the field shows while it is not being typed in. */
  export let text = '';
  /** What typing starts from — the bare number. */
  export let raw = '';
  export let label = '';
  export let decLabel;
  export let incLabel;
  export let disabled = false;
  /** Every value is at or below zero (a level in dBFS): a number typed without a sign is negative. */
  export let negative = false;
  /** Values on both sides of zero: a keyboard with a minus (a phone's decimal pad may have none). */
  export let signed = false;

  const dispatch = createEventDispatcher();
  let typing = false;
  let draft = '';

  const parse = (s) => (negative ? parseNonPositive(s) : parseNumber(s));

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

<div class="nf" class:disabled>
  <IconButton icon="remove" label={decLabel} {disabled} on:click={() => dispatch('dec')} />
  <label class="body">
    <span class="cap">{caption}</span>
    <input
      type="text"
      inputmode={signed ? 'text' : 'decimal'}
      aria-label={label || caption}
      {disabled}
      value={typing ? draft : text}
      on:focus={focus}
      on:input={(e) => (draft = e.target.value)}
      on:blur={commit}
      on:keydown={key}
    />
  </label>
  <IconButton icon="add" label={incLabel} {disabled} on:click={() => dispatch('inc')} />
</div>

<style>
  .nf {
    background: var(--surface-container);
    border-radius: var(--r-m);
    display: flex;
    align-items: center;
    gap: 2px;
    min-height: 56px;
    padding: 4px;
    min-width: 0;
  }
  .body {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    cursor: text;
  }
  .cap { font-size: 12px; line-height: 16px; color: var(--on-surface-var); }
  input {
    width: 100%;
    min-width: 0;
    border: none;
    background: none;
    color: var(--on-surface);
    font: inherit;
    font-size: 16px;
    line-height: 22px;
    font-weight: 500;
    text-align: center;
    padding: 0;
    border-radius: 4px;
  }
  input:focus { outline: 2px solid var(--primary); outline-offset: 2px; }
  .disabled .body { opacity: 0.45; }
</style>
