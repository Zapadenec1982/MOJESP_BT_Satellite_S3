<script>
  // One row of a group or a list: icon, title, a line under it, and whatever sits at the end
  // (a switch, a value, a button — the default slot). With `href` the whole row is a link and
  // shows a chevron; with `button` it is a button (listen to on:click). Panel: rows of 56
  // (settings) and 60 (lists), radius 20 / 16 (screen_set.cpp, ui_m3_widgets.cpp VList).
  import Icon from './Icon.svelte';

  export let icon = '';
  export let title = '';
  export let subtitle = '';
  export let href = '';
  export let button = false;
  /** The thing that is playing or chosen: secondary_container, like the panel's lists. */
  export let selected = false;
  export let disabled = false;
  /** Long-list density (60 px, radius 16) instead of a settings row (56 px, radius 20). */
  export let list = false;
</script>

{#if href && !disabled}
  <a class="row" class:list class:selected {href} on:click>
    {#if icon}<span class="lead"><Icon name={icon} /></span>{/if}
    <span class="txt">
      <span class="t">{title}<slot name="title" /></span>
      {#if subtitle || $$slots.subtitle}<span class="s">{subtitle}<slot name="subtitle" /></span>{/if}
    </span>
    <slot />
    <span class="chev"><Icon name="chevron_right" /></span>
  </a>
{:else if button}
  <button class="row" class:list class:selected type="button" {disabled} on:click>
    {#if icon}<span class="lead"><Icon name={icon} /></span>{/if}
    <span class="txt">
      <span class="t">{title}<slot name="title" /></span>
      {#if subtitle || $$slots.subtitle}<span class="s">{subtitle}<slot name="subtitle" /></span>{/if}
    </span>
    <slot />
  </button>
{:else}
  <div class="row" class:list class:selected class:disabled>
    {#if icon}<span class="lead"><Icon name={icon} /></span>{/if}
    <span class="txt">
      <span class="t">{title}<slot name="title" /></span>
      {#if subtitle || $$slots.subtitle}<span class="s">{subtitle}<slot name="subtitle" /></span>{/if}
    </span>
    <slot />
  </div>
{/if}

<style>
  .row {
    display: flex;
    align-items: center;
    gap: 14px;
    min-height: 56px;
    padding: 8px 12px 8px 14px;
    border-radius: var(--r-m);
    border: 0;
    background: none;
    color: var(--on-surface);
    text-decoration: none;
    text-align: left;
    width: 100%;
    position: relative;
  }
  .list {
    min-height: 60px;
    border-radius: var(--r-s);
    gap: 16px;
    padding: 8px 8px 8px 16px;
  }
  a.row::after,
  button.row::after {
    content: '';
    position: absolute;
    inset: 0;
    border-radius: inherit;
    background: var(--on-surface);
    opacity: 0;
    transition: opacity 120ms ease;
    pointer-events: none;
  }
  a.row:hover::after,
  button.row:hover:not(:disabled)::after { opacity: 0.06; }
  a.row:active::after,
  button.row:active:not(:disabled)::after { opacity: 0.1; }
  .lead { color: var(--on-surface-var); display: flex; flex: none; }
  .txt {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
  }
  /* up to two lines, then «…»: one line cut German and Polish words at 390 px */
  .t, .s {
    overflow: hidden;
    overflow-wrap: anywhere;
    display: -webkit-box;
    -webkit-box-orient: vertical;
    -webkit-line-clamp: 2;
    line-clamp: 2;
  }
  .t {
    font-size: 16px;
    line-height: 22px;
  }
  .s {
    font-size: 14px;
    line-height: 20px;
    color: var(--on-surface-var);
  }
  .chev { color: var(--on-surface-var); display: flex; flex: none; }
  .selected { background: var(--secondary-container); color: var(--on-secondary-container); }
  .selected .s, .selected .lead { color: var(--on-secondary-container); }
  .disabled, .row:disabled { opacity: 0.38; }
</style>
