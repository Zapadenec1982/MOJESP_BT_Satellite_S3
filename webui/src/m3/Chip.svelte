<script>
  // Assist / filter chip as on the panel (ui_m3_widgets.cpp:169-216): 32 px, radius 8, a hairline
  // ring; selected = secondary_container with a check unless an icon of its own is given.
  // Without a click handler or href it is a plain label (the format chip on the Player).
  import Icon from './Icon.svelte';

  export let icon = '';
  export let selected = false;
  export let href = '';
  /** Render as a button even without href (the parent listens to on:click). */
  export let button = false;
  /** A button chip that takes no tap for now (an edit on its way). */
  export let disabled = false;
</script>

{#if href}
  <a class="chip" class:selected {href} on:click>
    {#if icon}<Icon name={icon} size={18} fill={selected} />{:else if selected}<Icon name="check" size={18} />{/if}<slot />
  </a>
{:else if button}
  <button class="chip" class:selected type="button" aria-pressed={selected ? 'true' : 'false'} {disabled} on:click>
    {#if icon}<Icon name={icon} size={18} fill={selected} />{:else if selected}<Icon name="check" size={18} />{/if}<slot />
  </button>
{:else}
  <span class="chip" class:selected>
    {#if icon}<Icon name={icon} size={18} fill={selected} />{/if}<slot />
  </span>
{/if}

<style>
  .chip {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    height: 32px;
    padding: 0 12px;
    border-radius: 8px;
    border: 1px solid var(--outline-var);
    background: none;
    color: var(--on-surface-var);
    font-size: 14px;
    line-height: 20px;
    font-weight: 500;
    white-space: nowrap;
    text-decoration: none;
    flex: none;
  }
  .selected {
    background: var(--secondary-container);
    border-color: var(--secondary-container);
    color: var(--on-secondary-container);
  }
  a.chip, button.chip { cursor: pointer; }
  button.chip:disabled { opacity: 0.38; cursor: default; }
</style>
