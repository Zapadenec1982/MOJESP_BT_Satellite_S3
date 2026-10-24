<script>
  // Modal dialog as the panel draws it (ui_m3_widgets.cpp:791-828): black scrim at 50 %,
  // surface_container card, radius 28, padding 24, actions at the end. Esc or a tap on the
  // scrim dismisses it (`close`), unless `persistent`.
  import { createEventDispatcher, onMount, tick } from 'svelte';
  import { fade, scale } from 'svelte/transition';
  import { trap } from '../lib/trap.js';

  export let title = '';
  export let persistent = false;

  const dispatch = createEventDispatcher();
  let card;
  const id = `dlg-${Math.random().toString(36).slice(2, 8)}`;

  function close() {
    if (!persistent) dispatch('close');
  }

  onMount(async () => {
    await tick();
    // a text area too (the import's); never a hidden or a file input
    (card?.querySelector('[autofocus], button:not([disabled]), input:not([type=hidden]):not([type=file]), select, textarea, a[href]')
      || card)?.focus();
  });
</script>

<svelte:window on:keydown={(e) => e.key === 'Escape' && close()} />

<div class="scrim" transition:fade={{ duration: 150 }} on:click|self={close} role="presentation">
  <div
    class="card"
    bind:this={card}
    role="dialog"
    aria-modal="true"
    aria-labelledby={title ? id : null}
    tabindex="-1"
    use:trap
    transition:scale={{ start: 0.96, duration: 150 }}
  >
    {#if title}<h2 {id} class="title">{title}</h2>{/if}
    <div class="body"><slot /></div>
    {#if $$slots.actions}<div class="actions"><slot name="actions" /></div>{/if}
  </div>
</div>

<style>
  .scrim {
    position: fixed;
    inset: 0;
    z-index: 1000;
    background: rgb(0 0 0 / 50%);
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
  }
  .card {
    width: 100%;
    max-width: 440px;
    max-height: calc(100vh - 48px);
    overflow: auto;
    border-radius: var(--r-xl);
    background: var(--surface-container);
    color: var(--on-surface);
    padding: 24px;
    display: flex;
    flex-direction: column;
    gap: 16px;
  }
  .card:focus { outline: none; }
  .title { font-size: 22px; line-height: 28px; font-weight: 400; }
  .body { color: var(--on-surface-var); font-size: 15px; line-height: 22px; }
  .actions { display: flex; justify-content: flex-end; flex-wrap: wrap; gap: 8px; }
</style>
