<script>
  // M3 switch, 52×32: a 16 px knob off, a 24 px knob with a check on — the size the panel's
  // switch means to have (its LVGL rendering is off).
  import { createEventDispatcher } from 'svelte';
  import Icon from './Icon.svelte';

  export let checked = false;
  export let disabled = false;
  /** Accessible name, when no visible label is tied to it. */
  export let label = '';

  const dispatch = createEventDispatcher();

  function toggle() {
    if (disabled) return;
    checked = !checked;
    dispatch('change', checked);
  }
</script>

<button
  class="sw"
  class:on={checked}
  type="button"
  role="switch"
  aria-checked={checked ? 'true' : 'false'}
  aria-label={label || null}
  {disabled}
  on:click={toggle}
>
  <span class="knob">{#if checked}<Icon name="check" size={16} />{/if}</span>
</button>

<style>
  .sw {
    width: 52px;
    height: 32px;
    border-radius: 16px;
    border: 2px solid var(--outline);
    background: var(--surface-highest);
    position: relative;
    flex: none;
    padding: 0;
    transition: background-color 150ms ease, border-color 150ms ease;
  }
  .knob {
    position: absolute;
    top: 6px;
    left: 6px;
    width: 16px;
    height: 16px;
    border-radius: 12px;
    background: var(--outline);
    color: var(--primary);
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 150ms ease;
  }
  .on {
    background: var(--primary);
    border-color: var(--primary);
  }
  .on .knob {
    top: 2px;
    left: 22px;
    width: 24px;
    height: 24px;
    background: var(--on-primary);
  }
  .sw:disabled { opacity: 0.38; cursor: default; }
</style>
