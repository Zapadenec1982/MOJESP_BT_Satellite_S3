<script>
  // Stations: the panel's tabs — Favourites · Top · Countries · Genres · Search.
  // Favourites edit the list on the device; the catalog tabs come in their own file, fetched the
  // first time one is opened (rollup chunk), so the page that most visits never use it.
  import { t, K } from '../stores/i18n.js';
  import { go } from '../stores/router.js';
  import Tabs from '../m3/Tabs.svelte';
  import Favorites from './stations/Favorites.svelte';

  /** fav | top | countries | genres | search — the route's second segment */
  export let tab = 'fav';

  const TABS = [
    { id: 'fav', key: K('st.tab_fav') },
    { id: 'top', key: K('st.tab_top') },
    { id: 'countries', key: K('st.tab_countries') },
    { id: 'genres', key: K('st.tab_genres') },
    { id: 'search', key: K('st.tab_search') },
  ];

  // The tab that is open, tapped again: back to its list (a country's or a genre's stations
  // were a dead end — the address does not change, so ← left the catalog).
  let again = 0;
  function pick(id) {
    if (id === tab) again++;
    else go(id === 'fav' ? 'stations' : `stations/${id}`);
  }

  let catalog = null;
  $: if (tab !== 'fav' && !catalog) catalog = import('./stations/Catalog.svelte');

  $: items = TABS.map((x) => ({ id: x.id, label: $t[x.key] }));
</script>

<div class="tabs">
  <Tabs {items} value={tab} label={$t['page.stations']} on:change={(e) => pick(e.detail)} />
</div>

{#if tab === 'fav'}
  <Favorites />
{:else if catalog}
  {#await catalog}
    <p class="msg">{$t['app.loading']}</p>
  {:then mod}
    <svelte:component this={mod.default} {tab} {again} />
  {:catch e}
    <p class="msg">{e.message}</p>
  {/await}
{/if}

<style>
  .tabs { padding: 4px 0 4px; margin: 0 -4px; }
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px; }
</style>
