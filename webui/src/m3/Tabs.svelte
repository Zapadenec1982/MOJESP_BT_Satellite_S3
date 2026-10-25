<script>
  // Text tabs as on the panel's station pages (screen_catalog.cpp:836-846): the chosen one on
  // secondary_container. Scrolls sideways when it does not fit.
  import { createEventDispatcher } from 'svelte';

  /** @type {{id: string, label: string}[]} */
  export let items = [];
  export let value = '';
  export let label = '';

  const dispatch = createEventDispatcher();

  function pick(id) {
    value = id;
    dispatch('change', id);
  }

  function key(e, i) {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
    const next = items[(i + (e.key === 'ArrowRight' ? 1 : items.length - 1)) % items.length];
    pick(next.id);
    e.currentTarget.parentElement.children[items.indexOf(next)]?.focus();
  }
</script>

<div class="tabs" role="tablist" aria-label={label || null}>
  {#each items as it, i (it.id)}
    <button
      class="tab"
      class:on={it.id === value}
      type="button"
      role="tab"
      aria-selected={it.id === value ? 'true' : 'false'}
      tabindex={it.id === value ? 0 : -1}
      on:click={() => pick(it.id)}
      on:keydown={(e) => key(e, i)}
    >{it.label}</button>
  {/each}
</div>

<style>
  .tabs {
    display: flex;
    gap: 4px;
    overflow-x: auto;
    scrollbar-width: none;
  }
  .tabs::-webkit-scrollbar { display: none; }
  .tab {
    height: 40px;
    padding: 0 16px;
    border-radius: 20px;
    border: 0;
    background: none;
    color: var(--on-surface-var);
    font-size: 15px;
    font-weight: 500;
    white-space: nowrap;
    flex: none;
  }
  .on { background: var(--secondary-container); color: var(--on-secondary-container); }
  /* a phone: the five of Станції in one row in every language (Suche, Szukaj went off its edge) */
  @media (max-width: 420px) {
    .tab { padding: 0 10px; font-size: 14px; }
  }
</style>
