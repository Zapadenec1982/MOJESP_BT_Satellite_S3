<script context="module">
  // The index as last read, kept while the app is open: a second visit asks only whether the card
  // changed (its generation and count) and shows the list at once.
  let cache = null;   // { gen, count, root, rows: [path under the root] }
</script>

<script>
  // The card's library (W6, PLAN_WEBUI §2): every track on the card, as the panel's Library page
  // lists them (screen_library.cpp) — the file name without its extension, its folder under it,
  // the track the queue is on marked. A filter narrows it by name or folder; a tap plays the
  // track (player.track + player.cmd=1, what the panel posts) and the Player opens, as on the glass.
  //
  // GET /api/library gives the index a page at a time; the page reads all of it, then filters in
  // the browser, where Cyrillic letters fold to lower case properly.
  import { onMount, onDestroy, tick } from 'svelte';
  import { state } from '../stores/state.js';
  import { t, fmt, plural, language, K } from '../stores/i18n.js';
  import { go } from '../stores/router.js';
  import { apiGet, apiPost } from '../lib/api.js';
  import { trackName, trackFolder, trackPath, matches } from '../lib/media.js';
  import { toastError } from '../stores/toast.js';
  import Group from '../m3/Group.svelte';
  import Row from '../m3/Row.svelte';
  import Icon from '../m3/Icon.svelte';
  import Button from '../m3/Button.svelte';
  import TextField from '../m3/TextField.svelte';

  K('srcsub.tracks.one'); K('srcsub.tracks.few'); K('srcsub.tracks.many'); K('srcsub.tracks.other');

  const PAGE = 200;
  const STEP = 150;

  let lib = cache;
  let loaded = cache ? cache.rows.length : 0;
  let total = cache ? cache.count : 0;
  let busy = false;          // the panel is still reading the card
  let err = '';
  let query = '';
  let limit = STEP;
  let alive = true;
  let serial = 0;
  let retry = null;

  const sleep = (ms) => new Promise((r) => { retry = setTimeout(r, ms); });

  /** The whole index, page after page; starts over when the card changed under it. */
  async function load() {
    const mine = ++serial;
    err = '';
    for (let attempt = 0; attempt < 40 && alive && mine === serial; attempt++) {
      try {
        const probe = !!cache;       // only "has it changed": one row
        const first = await apiGet(`/api/library?from=0&n=${probe ? 1 : PAGE}`);
        if (mine !== serial) return;
        if (first.busy) {
          busy = true;
          await sleep(1500);
          continue;
        }
        busy = false;
        if (probe && cache.gen === first.gen && cache.count === first.count) {
          lib = cache;
          loaded = total = cache.count;
          return;
        }
        let rows = probe ? [] : first.rows;
        total = first.count;
        loaded = rows.length;
        let from = rows.length;
        let moved = false;
        while (from < first.count && alive && mine === serial) {
          const p = await apiGet(`/api/library?from=${from}&n=${PAGE}`);
          if (p.busy || p.gen !== first.gen || !p.rows.length) { moved = true; break; }
          rows = rows.concat(p.rows);
          from += p.rows.length;
          loaded = from;
        }
        if (mine !== serial) return;
        if (moved) { cache = null; await sleep(1000); continue; }
        cache = { gen: first.gen, count: first.count, root: first.root, rows };
        lib = cache;
        await reveal();
        return;
      } catch (e) {
        if (mine === serial && e.message !== 'Unauthorized') err = e.message;
        return;
      }
    }
  }

  // The panel rebuilds the index when a card appears; player.count is how the web hears of it.
  let seenCount;
  $: onCount($state['player.count']);
  function onCount(c) {
    if (c === undefined) return;
    if (seenCount !== undefined && c !== seenCount) load();
    seenCount = c;
  }

  $: cursor = $state['player.index'] ?? -1;
  $: playing = $state['audio.source'] === 4 && $state['player.state'] === 'playing';
  $: rows = lib ? lib.rows.map((rel, i) => ({ rel, i })) : [];
  $: found = query ? rows.filter((r) => matches(r.rel, query)) : rows;
  $: shown = found.slice(0, limit);
  $: query, (limit = STEP);      // a new filter starts from its top

  /** Opens on the track the queue is on, once, as the panel does. */
  async function reveal() {
    if (!lib || cursor < 0 || cursor >= lib.rows.length) return;
    limit = Math.max(limit, cursor + 20);
    await tick();
    const el = document.getElementById(`lib-${cursor}`);
    if (el) el.scrollIntoView({ block: 'center' });
  }

  async function play(r) {
    try {
      // the path first, then the command, in one body: the player reads the one with the other
      await apiPost('/api/settings', { 'player.track': trackPath(lib.root, r.rel), 'player.cmd': 1 });
      go('player');
    } catch (e) {
      toastError(e.message);
    }
  }

  onMount(() => {
    if (lib) reveal();
    load();
  });
  onDestroy(() => {
    alive = false;
    clearTimeout(retry);
  });
</script>

<div class="top">
  <TextField label={$t['lib.filter']} placeholder={$t['lib.filter_ph']} bind:value={query} />
  <p class="count">
    {#if busy}
      {$t['lib.busy']}
    {:else if lib && query}
      {fmt($t['lib.found'], { n: found.length, total: lib.rows.length })}
    {:else if lib}
      {plural($t, $language, 'srcsub.tracks', lib.rows.length)}
    {:else if total}
      {fmt($t['lib.reading'], { n: loaded, total })}
    {/if}
  </p>
</div>

{#if err}
  <p class="msg err">{fmt($t['lib.unavailable'], { e: err })}</p>
  <div class="center"><Button variant="tonal" icon="refresh" on:click={load}>{$t['app.retry']}</Button></div>
{:else if !lib}
  {#if !busy && !total}<p class="msg">{$t['app.loading']}</p>{/if}
{:else if !lib.rows.length}
  <p class="msg">{$t['lib.empty']}</p>
{:else if !found.length}
  <p class="msg">{$t['lib.nothing']}</p>
{:else}
  <Group>
    {#each shown as r (r.i)}
      <div id="lib-{r.i}">
        <Row list button icon="music_note" selected={r.i === cursor} title={trackName(r.rel) || '—'}
             subtitle={trackFolder(r.rel)} on:click={() => play(r)}>
          {#if r.i === cursor && playing}<span class="eq"><Icon name="graphic_eq" label={$t['lib.playing']} /></span>{/if}
        </Row>
      </div>
    {/each}
  </Group>
  {#if found.length > shown.length}
    <div class="center more">
      <Button variant="tonal" on:click={() => (limit += STEP)}>{fmt($t['lib.more'], { n: found.length - shown.length })}</Button>
    </div>
  {/if}
{/if}

<style>
  .top { padding: 8px 0 4px; display: flex; flex-direction: column; gap: 4px; }
  .count { padding: 4px 16px; font-size: 14px; line-height: 20px; font-weight: 500; color: var(--on-surface-var); min-height: 28px; }
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px 16px; }
  .msg.err { color: var(--error); }
  .center { display: flex; justify-content: center; }
  .more { padding: 12px 0; }
  .eq { display: flex; flex: none; color: var(--on-secondary-container); }
</style>
