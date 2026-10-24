<script>
  // Icon-only button. `label` is required: it is the button's only name for a screen reader.
  import Icon from './Icon.svelte';

  export let icon;
  export let label;
  /** @type {'standard'|'tonal'|'filled'|'outlined'} */
  export let variant = 'standard';
  /** Toggle state, e.g. repeat or shuffle on: the glyph turns primary and filled. */
  export let selected = false;
  export let size = 24;
  export let href = '';
  export let disabled = false;
</script>

{#if href && !disabled}
  <a class="ib {variant}" class:selected {href} aria-label={label} title={label} on:click>
    <Icon name={icon} {size} fill={selected} />
  </a>
{:else}
  <button
    class="ib {variant}"
    class:selected
    type="button"
    aria-label={label}
    title={label}
    aria-pressed={variant === 'standard' && selected ? 'true' : null}
    {disabled}
    on:click
  >
    <Icon name={icon} {size} fill={selected} />
  </button>
{/if}

<style>
  .ib {
    width: 48px;
    height: 48px;
    border-radius: 24px;
    border: 0;
    padding: 0;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex: none;
    background: none;
    color: var(--on-surface-var);
    position: relative;
    text-decoration: none;
  }
  .ib::after {
    content: '';
    position: absolute;
    inset: 4px;
    border-radius: 20px;
    background: currentColor;
    opacity: 0;
    transition: opacity 120ms ease;
    pointer-events: none;
  }
  .ib:hover::after { opacity: 0.08; }
  .ib:active::after { opacity: 0.12; }
  .selected { color: var(--primary); }
  .tonal { background: var(--secondary-container); color: var(--on-secondary-container); }
  .filled { background: var(--primary); color: var(--on-primary); }
  .outlined { box-shadow: inset 0 0 0 1px var(--outline-var); color: var(--primary); }
  .tonal::after, .filled::after, .outlined::after { inset: 0; border-radius: inherit; }
  .ib:disabled { opacity: 0.38; cursor: default; }
  .ib:disabled::after { opacity: 0; }
</style>
