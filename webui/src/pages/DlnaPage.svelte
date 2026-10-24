<script context="module">
  // The path this tab opened its way down, kept while the app is open: coming back to the page
  // finds the crumbs where they were (lib/media.js nextCrumbs()).
  let kept = [];
</script>

<script>
  // DLNA media servers (W6, PLAN_WEBUI §2): servers › folders › tracks, the browser the panel's
  // DLNA page drives (components/dlna_client) — the same snapshot, GET /api/dlna/browser, and the
  // same revision-stamped commands, dlna.browse_cmd (screen_dlna.cpp). One browser for both: a
  // folder opened on the glass is the folder this page shows.
  //
  // The snapshot is not in the WebSocket's state, so it is asked for: every 0.4 s while something
  // is being fetched or a command is on its way, every 4 s otherwise, never in a hidden tab.
  import { onMount, onDestroy } from 'svelte';
  import { t, fmt, msg } from '../stores/i18n.js';
  import { go } from '../stores/router.js';
  import { apiGet, apiPost } from '../lib/api.js';
  import { BROWSE, browseCode, nextCrumbs } from '../lib/media.js';
  import { toastError, toastSuccess } from '../stores/toast.js';
  import Group from '../m3/Group.svelte';
  import Row from '../m3/Row.svelte';
  import Icon from '../m3/Icon.svelte';
  import IconButton from '../m3/IconButton.svelte';
  import Button from '../m3/Button.svelte';

  let view = null;
  let err = '';
  let crumbs = kept;
  $: kept = crumbs;
  let expect = null;            // what the command on its way should do to the path
  let sentRev = null;           // the revision it was sent from; null when none is on its way
  let sentAt = 0;
  let sentCmd = 0;
  let applied = -1;             // the revision the crumbs were last fitted to
  let climbing = 0;             // ← still to send after this one, to reach a crumb
  let discovered = false;
  let timer = null;
  let alive = true;
  let inflight = false;         // one request at a time, so answers cannot arrive out of order
  let again = false;

  $: waiting = !view || view.busy || sentRev !== null;

  async function poll() {
    clearTimeout(timer);
    if (inflight) {
      again = true;
      return;
    }
    inflight = true;
    if (document.visibilityState === 'visible') {
      try {
        const v = await apiGet('/api/dlna/browser');
        if (alive) take(v);
        err = '';
      } catch (e) {
        if (e.message !== 'Unauthorized') err = e.message;
      }
    }
    inflight = false;
    if (!alive) return;
    const next = again ? 0 : view && (view.busy || sentRev !== null) ? 400 : 4000;
    again = false;
    timer = setTimeout(poll, next);
  }
  const soon = () => {
    if (!alive) return;
    clearTimeout(timer);
    timer = setTimeout(poll, 250);
  };

  function take(v) {
    view = v;
    if (v.busy) return;
    // The snapshot the command was chosen from is not its answer. A command the browser turned
    // down (its revision had moved) changes nothing, so after 3 s the page stops waiting.
    if (sentRev !== null && v.revision === sentRev && Date.now() - sentAt < 3000) return;
    if (v.revision !== applied) {
      crumbs = nextCrumbs(crumbs, expect, v);
      applied = v.revision;
      // a page or a folder this tab asked for starts at its top
      if (sentRev !== null) window.scrollTo(0, 0);
    }
    expect = null;
    sentRev = null;
    if (climbing > 0 && !v.servers) {
      climbing--;
      send(BROWSE.back, 0, { back: true });
    } else {
      climbing = 0;
    }
    // Servers are looked for when the page opens and none have been found yet, as on the glass.
    if (!discovered && v.servers && v.count === 0) {
      discovered = true;
      send(BROWSE.discover);
    }
    discovered = true;
  }

  async function send(command, row = 0, exp = null) {
    if (!view) return;
    const code = browseCode(command, row, view.revision);
    // Queue and Cancel only change the status line; the rest publish a new revision.
    const moves = command !== BROWSE.queue && command !== BROWSE.cancel;
    if (moves) {
      sentRev = view.revision;
      sentAt = Date.now();
      sentCmd = command;
      expect = exp;
    }
    try {
      await apiPost('/api/settings', { 'dlna.browse_cmd': code });
    } catch (e) {
      sentRev = null;
      expect = null;
      climbing = 0;
      toastError(e.message);
      return;
    }
    soon();
  }

  function open(row, i) {
    if (waiting) return;
    send(BROWSE.open, i, row.folder ? { open: row.title } : null);
    // A track plays, and the Player is where that is shown — as the panel goes there.
    if (!row.folder && row.playable) go('player');
  }

  function queue(row, i) {
    if (waiting) return;
    send(BROWSE.queue, i);
    toastSuccess(fmt($t['dlna.queued'], { title: row.title }));
  }

  // "Media servers" climbs until the servers' list is reached, however deep the path this tab does
  // not know: the browser keeps sixteen levels (DEPTH, dlna_browser.cpp), and the top one more.
  const TO_SERVERS = 17;

  /** A crumb: `levels` up — one ← at a time, each once the last has landed. */
  function climb(levels) {
    if (waiting || levels < 1) return;
    climbing = levels - 1;
    send(BROWSE.back, 0, { back: true });
  }

  // The device's status line is a message of the catalog (dlna_browser.cpp's publish(), discover()
  // and browse()); the routine ones — counts, the search, loading, what was queued or plays — this
  // page says in its own way, the rest (an error, "cancelled") it shows in its language.
  const ROUTINE = /^@(dlna\.st_servers|dlna\.st_page|dlna\.searching|app\.loading|dlna\.queued|dlna\.st_playing)(\u001f|$)/;
  $: searching = view && (view.busy ? /^@dlna\.searching(\u001f|$)/.test(view.status || '') : sentCmd === BROWSE.discover);
  $: status = !view ? ''
    : waiting ? (searching ? $t['dlna.searching'] : $t['app.loading'])
    : ROUTINE.test(view.status || '') ? '' : msg($t, view.status);

  function onVisible() {
    if (document.visibilityState === 'visible') poll();
  }
  onMount(() => {
    poll();
    document.addEventListener('visibilitychange', onVisible);
  });
  onDestroy(() => {
    alive = false;
    clearTimeout(timer);
    document.removeEventListener('visibilitychange', onVisible);
  });
</script>

<nav class="crumbs" aria-label={$t['dlna.path']}>
  {#if view && !view.servers}
    <button class="crumb" type="button" disabled={waiting} on:click={() => climb(TO_SERVERS)}>{$t['dlna.servers']}</button>
    {#each crumbs as c, i}
      <Icon name="chevron_right" size={18} />
      {#if i === crumbs.length - 1}
        <span class="crumb here" aria-current="location">{c ?? msg($t, view.title)}</span>
      {:else}
        <button class="crumb" type="button" disabled={waiting} title={c === null ? $t['dlna.up'] : c}
                on:click={() => climb(crumbs.length - 1 - i)}>{c ?? '…'}</button>
      {/if}
    {/each}
  {:else}
    <span class="crumb here" aria-current="location">{$t['dlna.servers']}</span>
  {/if}
  <span class="acts">
    {#if view && view.busy}
      <IconButton icon="close" label={$t['common.cancel']} on:click={() => send(BROWSE.cancel)} />
    {:else if view && view.servers}
      <IconButton icon="refresh" label={$t['dlna.find']} disabled={waiting} on:click={() => send(BROWSE.discover)} />
    {/if}
  </span>
</nav>

{#if status}<p class="status" class:busy={waiting}>{status}</p>{/if}

{#if err && !view}
  <p class="msg err">{fmt($t['dlna.unavailable'], { e: err })}</p>
  <div class="center"><Button variant="tonal" icon="refresh" on:click={poll}>{$t['app.retry']}</Button></div>
{:else if !view}
  <p class="msg">{$t['app.loading']}</p>
{:else if view.count === 0 && !waiting}
  <p class="msg">{view.servers ? $t['dlna.none'] : $t['dlna.empty']}</p>
  {#if view.servers}
    <div class="center"><Button variant="tonal" icon="refresh" disabled={waiting} on:click={() => send(BROWSE.discover)}>{$t['dlna.find']}</Button></div>
  {/if}
{:else if view.count > 0}
  <Group>
    {#each view.rows as row, i (view.revision + ':' + i)}
      <div class="item" class:busy={waiting}>
        <Row list button disabled={waiting || (!row.folder && !row.playable)}
             icon={view.servers ? 'hub' : row.folder ? 'folder' : 'music_note'}
             title={row.title || '—'}
             subtitle={row.folder ? '' : row.playable ? row.artist : $t['dlna.unsupported']}
             on:click={() => open(row, i)}>
          {#if row.folder}<span class="chev"><Icon name="chevron_right" /></span>{/if}
        </Row>
        {#if !row.folder && row.playable}
          <IconButton icon="add" label={$t['dlna.queue']} disabled={waiting} on:click={() => queue(row, i)} />
        {/if}
      </div>
    {/each}
  </Group>
  {#if !view.servers && (view.previous || view.next)}
    <div class="pager">
      <IconButton icon="chevron_left" label={$t['dlna.prev']} disabled={waiting || !view.previous} on:click={() => send(BROWSE.prev)} />
      <IconButton icon="chevron_right" label={$t['dlna.next']} disabled={waiting || !view.next} on:click={() => send(BROWSE.next)} />
    </div>
  {/if}
{/if}

<style>
  .crumbs {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 2px 4px;
    padding: 8px 0 4px 8px;
    color: var(--on-surface-var);
    min-height: 56px;
  }
  .crumb {
    border: 0;
    background: none;
    color: var(--primary);
    font-size: 15px;
    line-height: 22px;
    padding: 6px 6px;
    border-radius: var(--r-xs);
    max-width: 14em;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  button.crumb:hover:not(:disabled) { background: color-mix(in srgb, var(--primary) 8%, transparent); }
  button.crumb:disabled { color: var(--on-surface-var); }
  .crumb.here { color: var(--on-surface); font-weight: 500; }
  .acts { display: flex; margin-left: auto; }
  .status { padding: 0 16px 8px; font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
  .status.busy { color: var(--primary); }
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px 16px; }
  .msg.err { color: var(--error); }
  .center { display: flex; justify-content: center; }
  .item { display: flex; align-items: center; min-width: 0; }
  .item :global(.row) { flex: 1; min-width: 0; }
  .chev { color: var(--on-surface-var); display: flex; flex: none; }
  .pager { display: flex; justify-content: center; gap: 24px; padding: 12px 0; }
</style>
