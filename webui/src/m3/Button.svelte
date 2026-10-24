<script>
  // M3 common button as the panel draws it (ui_m3_widgets.cpp): filled = primary,
  // tonal = secondary_container, text = primary on nothing, outlined = a hairline ring.
  import Icon from './Icon.svelte';

  /** @type {'filled'|'tonal'|'text'|'outlined'} */
  export let variant = 'filled';
  export let icon = '';
  export let href = '';
  export let type = 'button';
  export let disabled = false;
  /** Paint it in error: a destructive action such as a factory reset. */
  export let danger = false;
</script>

{#if href && !disabled}
  <a class="btn {variant}" class:danger class:with-icon={icon} {href} on:click>
    {#if icon}<Icon name={icon} size={18} />{/if}<slot />
  </a>
{:else}
  <button class="btn {variant}" class:danger class:with-icon={icon} {type} {disabled} on:click>
    {#if icon}<Icon name={icon} size={18} />{/if}<slot />
  </button>
{/if}

<style>
  .btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    min-height: 40px;
    padding: 0 24px;
    border: 0;
    border-radius: 20px;
    font-size: 15px;
    font-weight: 500;
    line-height: 20px;
    text-decoration: none;
    white-space: nowrap;
    flex: none;
    position: relative;
    transition: background-color 120ms ease, box-shadow 120ms ease;
  }
  .with-icon { padding-left: 16px; }
  .filled { background: var(--primary); color: var(--on-primary); }
  .tonal { background: var(--secondary-container); color: var(--on-secondary-container); }
  .text { background: none; color: var(--primary); padding: 0 12px; }
  .outlined { background: none; color: var(--primary); border: 1px solid var(--outline-var); }
  .danger.text, .danger.outlined { color: var(--error); }
  .danger.outlined { border-color: var(--error); }
  .danger.filled { background: var(--error); color: var(--surface); }

  /* M3 state layer: the content colour at 8 % on hover, 12 % on press */
  .btn::after {
    content: '';
    position: absolute;
    inset: 0;
    border-radius: inherit;
    background: currentColor;
    opacity: 0;
    transition: opacity 120ms ease;
    pointer-events: none;
  }
  .btn:hover::after { opacity: 0.08; }
  .btn:active::after { opacity: 0.12; }

  .btn:disabled {
    cursor: default;
    opacity: 0.38;
  }
  .btn:disabled::after { opacity: 0; }
</style>
