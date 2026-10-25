<script>
  // The Radio Browser catalog — Top · Countries · Genres · Search — what data/www/radio.html was,
  // inside the Stations page. The browser asks Radio Browser itself; the panel
  // gets a station's name, stream and uuid through radio.web_* (modules/radio/src/catalog.cpp),
  // with the action last: play, keep in the list, keep in preset N. Loaded only when opened.
  import { onDestroy } from 'svelte';
  import { t, fmt, plural, language, K } from '../../stores/i18n.js';
  import { apiPost } from '../../lib/api.js';
  import { fitName } from '../../lib/stations.js';
  import * as RB from '../../lib/radiobrowser.js';
  import { toastSuccess, toastError, toastWarn } from '../../stores/toast.js';
  import Chip from '../../m3/Chip.svelte';
  import Switch from '../../m3/Switch.svelte';
  import Row from '../../m3/Row.svelte';
  import Group from '../../m3/Group.svelte';
  import IconButton from '../../m3/IconButton.svelte';
  import Button from '../../m3/Button.svelte';
  import Dialog from '../../m3/Dialog.svelte';
  import TextField from '../../m3/TextField.svelte';

  /** top | countries | genres | search */
  export let tab = 'top';

  // the plural forms used through plural()
  K('cat.votes.one'); K('cat.votes.few'); K('cat.votes.many'); K('cat.votes.other');
  K('cat.countries.one'); K('cat.countries.few'); K('cat.countries.many'); K('cat.countries.other');
  K('cat.genres.one'); K('cat.genres.few'); K('cat.genres.many'); K('cat.genres.other');
  K('cat.stations.one'); K('cat.stations.few'); K('cat.stations.many'); K('cat.stations.other');

  const SORTS = [K('cat.sort_pop'), K('cat.sort_votes'), K('cat.sort_quality'), K('cat.sort_az')];
  const FLOORS = [K('cat.q_all'), K('cat.q_128'), K('cat.q_192'), K('cat.q_320'), K('cat.q_flac')];

  const remembered = (key, dflt) => {
    try { const v = localStorage.getItem(key); return v === null ? dflt : v; } catch (e) { return dflt; }
  };
  const remember = (key, v) => { try { localStorage.setItem(key, String(v)); } catch (e) { /* no storage */ } };

  // the same keys radio.html kept them under
  let sort = Number(remembered('radio.sort', '0'));
  let floor = Number(remembered('radio.floor', '1'));
  let uaOnly = remembered('radio.ua_only', '0') === '1';

  let view = 'stations';       // stations | countries | genres | search
  let spec = { country: 'UA' };
  let where = '';
  let offset = 0;
  let rows = [];
  let more = false;
  let msg = '';
  let err = false;
  let serial = 0;
  let query = '';
  let countryList = null;
  let genreList = null;
  let presetFor = null;        // a row waiting for its preset number

  $: lang = $language;
  $: region = (() => {
    try {
      const d = new Intl.DisplayNames([lang], { type: 'region' });
      return (c) => { try { return d.of(c.toUpperCase()) || c; } catch (e) { return c; } };
    } catch (e) {
      return (c) => c;
    }
  })();
  $: num = new Intl.NumberFormat(lang);

  async function showStations() {
    view = 'stations';
    const mine = ++serial;
    msg = $t['app.loading'];
    err = false;
    rows = [];
    try {
      const r = await RB.stations(spec, { sort, floor, uaOnly, offset });
      if (mine !== serial) return;
      rows = r.rows;
      more = r.more;
      msg = rows.length ? '' : more ? $t['cat.nothing_page'] : $t['cat.nothing'];
    } catch (e) {
      if (mine !== serial) return;
      msg = fmt($t['cat.unavailable'], { e: e.message });
      err = true;
    }
    window.scrollTo(0, 0);
  }

  async function showCountries() {
    view = 'countries';
    const mine = ++serial;
    err = false;
    if (!countryList) {
      msg = $t['app.loading'];
      try {
        countryList = await RB.countries();
      } catch (e) {
        if (mine === serial) { msg = fmt($t['cat.unavailable'], { e: e.message }); err = true; }
        return;
      }
    }
    if (mine === serial) msg = '';
  }

  async function showGenres() {
    view = 'genres';
    const mine = ++serial;
    err = false;
    if (!genreList) {
      msg = $t['app.loading'];
      try {
        genreList = await RB.genres();
      } catch (e) {
        if (mine === serial) { msg = $t['cat.no_genres']; err = true; }
        return;
      }
    }
    if (mine === serial) msg = '';
  }

  function open(which) {
    offset = 0;
    stopPreview();
    if (which === 'top') {
      spec = { country: 'UA' };
      where = region('UA');
      showStations();
    } else if (which === 'countries') {
      showCountries();
    } else if (which === 'genres') {
      showGenres();
    } else {
      view = 'search';
      rows = [];
      msg = $t['cat.search_hint'];
      err = false;
    }
  }

  /** Bumps when the open tab is tapped again: its list once more (StationsPage). */
  export let again = 0;
  let opened = null;
  let seen = 0;
  $: if (tab !== opened) { opened = tab; seen = again; open(tab); }
  $: if (again !== seen) { seen = again; open(tab); }

  function pickCountry(c) {
    spec = { country: c.code };
    where = region(c.code);
    offset = 0;
    showStations();
  }
  function pickGenre(g) {
    spec = { tag: g.tag };
    where = RB.genreName(g, lang);
    offset = 0;
    showStations();
  }
  function search() {
    const words = query.trim();
    if (!words) { toastWarn($t['cat.search_hint']); return; }
    spec = { name: words };
    where = `«${words}»`;
    offset = 0;
    showStations();
  }

  function refilter() {
    remember('radio.sort', sort);
    remember('radio.floor', floor);
    remember('radio.ua_only', uaOnly ? '1' : '0');
    if (view === 'stations') { offset = 0; showStations(); }
  }

  function page(dir) {
    offset = Math.max(0, offset + dir * RB.LIMIT);
    showStations();
  }

  // Reactive for the same reason as Favorites' subtitle: it reads $t, and a plain function
  // called from the template would keep the first catalog's words.
  $: sub = (st) => {
    const codec = (st.codec || '').toUpperCase();
    const parts = [codec === 'FLAC' ? `FLAC · ${$t['cat.lossless']}` : st.bitrate ? `${codec} ${st.bitrate}` : codec];
    if (st.countrycode) parts.push(region(st.countrycode));
    if (sort === 0) parts.push(fmt($t['cat.per_day'], { n: num.format(st.clickcount || 0) }));
    if (sort === 1) parts.push(plural($t, lang, 'cat.votes', st.votes || 0));
    if (st.tags) parts.push(st.tags.split(',').slice(0, 3).join(', '));
    return parts.join(' · ');
  };

  /** act: 0 play, 1 keep in the list, 2..7 keep in preset 1..6 (radio.web_act). */
  async function toPanel(st, act) {
    try {
      await apiPost('/api/settings', {
        'radio.web_name': fitName(st.name),
        'radio.web_url': st.url,
        'radio.web_uuid': st.stationuuid || '',
        'radio.web_act': act,          // last: the panel acts once the other three are written
      });
      toastSuccess(fmt(act === 0 ? $t['cat.played'] : act === 1 ? $t['cat.kept'] : $t['cat.in_preset'],
                       { name: st.name, n: act - 1 }));
    } catch (e) {
      toastError(e.message);
    }
  }

  // ── listening in the browser, one station at a time ──
  let audio;
  let previewing = null;
  function preview(st) {
    if (previewing === st.url) { stopPreview(); return; }
    audio.src = st.url;
    previewing = st.url;
    audio.play().catch(() => { toastWarn($t['cat.no_play']); stopPreview(); });
  }
  function stopPreview() {
    if (!audio) return;
    audio.pause();
    audio.removeAttribute('src');
    previewing = null;
  }
  onDestroy(stopPreview);
</script>

<audio bind:this={audio} preload="none"></audio>

{#if view === 'search' || (view === 'stations' && spec.name)}
  <form class="search" on:submit|preventDefault={search}>
    <TextField label={$t['cat.search_ph']} bind:value={query} maxlength={60} />
    <Button icon="search" type="submit">{$t['cat.search']}</Button>
  </form>
{/if}

{#if view === 'stations' || view === 'search'}
  <div class="filters">
    {#each SORTS as key, i}
      <Chip button selected={sort === i} on:click={() => { sort = i; refilter(); }}>{$t[key]}</Chip>
    {/each}
    <label class="sel">
      <span class="visually-hidden">{$t['cat.quality']}</span>
      <select bind:value={floor} on:change={refilter} aria-label={$t['cat.quality']}>
        {#each FLOORS as key, i}<option value={i}>{$t[key]}</option>{/each}
      </select>
    </label>
    {#if tab === 'genres' || tab === 'search'}
      <span class="ua"><Switch checked={uaOnly} label={$t['cat.ua_only']} on:change={(e) => { uaOnly = e.detail; refilter(); }} /><span>{$t['cat.ua_only']}</span></span>
    {/if}
  </div>
{/if}

{#if view === 'stations' && where}<p class="where">{where}</p>{/if}

{#if msg}<p class="msg" class:err>{msg}</p>{/if}

{#if view === 'countries' && countryList && !msg}
  <p class="where">{plural($t, lang, 'cat.countries', countryList.length)}</p>
  <Group>
    {#each countryList as c (c.code)}
      <Row list button icon="language" title={region(c.code)} subtitle={plural($t, lang, 'cat.stations', c.n)} on:click={() => pickCountry(c)} />
    {/each}
  </Group>
{:else if view === 'genres' && genreList && !msg}
  <p class="where">{plural($t, lang, 'cat.genres', genreList.length)}</p>
  <Group>
    {#each genreList as g (g.tag)}
      <Row list button icon="music_note" title={RB.genreName(g, lang)} on:click={() => pickGenre(g)} />
    {/each}
  </Group>
{:else if view === 'stations' && rows.length}
  <Group>
    {#each rows as st (st.url)}
      <div class="item">
        <button class="main" type="button" on:click={() => toPanel(st, 0)} title={$t['cat.play']}>
          <span class="logo">{#if st.favicon}<img src={st.favicon} alt="" loading="lazy" referrerpolicy="no-referrer"
            on:error={(e) => e.currentTarget.remove()} />{/if}</span>
          <span class="txt"><span class="t">{st.name}</span><span class="s">{sub(st)}</span></span>
        </button>
        <IconButton icon={previewing === st.url ? 'stop' : 'headphones'} selected={previewing === st.url}
                    label={previewing === st.url ? $t['cat.listen_stop'] : $t['cat.listen']} on:click={() => preview(st)} />
        <IconButton icon="favorite" label={$t['cat.keep']} on:click={() => toPanel(st, 1)} />
        <IconButton icon="more_vert" label={$t['cat.to_preset']} on:click={() => (presetFor = st)} />
      </div>
    {/each}
  </Group>
  <div class="pager">
    <IconButton icon="chevron_left" label={$t['cat.prev']} disabled={offset === 0} on:click={() => page(-1)} />
    <span>{fmt($t['cat.page'], { n: offset / RB.LIMIT + 1 })}</span>
    <IconButton icon="chevron_right" label={$t['cat.next']} disabled={!more} on:click={() => page(1)} />
  </div>
{/if}

{#if presetFor}
  <Dialog title={presetFor.name} on:close={() => (presetFor = null)}>
    <p>{$t['cat.to_preset']}</p>
    <div class="nums">
      {#each Array(6) as _, i}
        <Button variant="tonal" on:click={() => { toPanel(presetFor, i + 2); presetFor = null; }}>{i + 1}</Button>
      {/each}
    </div>
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (presetFor = null)}>{$t['common.cancel']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .search { display: flex; align-items: flex-end; gap: 8px; padding: 8px 0; }
  .search :global(.tf) { flex: 1; }
  .filters { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; padding: 8px 0; }
  .sel select {
    height: 32px;
    border-radius: var(--r-xs);
    border: 1px solid var(--outline-var);
    background: var(--surface-container);
    color: var(--on-surface);
    padding: 0 8px;
    font-size: 14px;
  }
  .ua { display: inline-flex; align-items: center; gap: 8px; font-size: 14px; color: var(--on-surface-var); }
  .where { padding: 8px 16px 4px; font-size: 14px; font-weight: 500; color: var(--on-surface-var); }
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px; }
  .msg.err { color: var(--error); }
  .item { display: flex; align-items: center; }
  .main {
    flex: 1;
    min-width: 0;
    display: flex;
    align-items: center;
    gap: 12px;
    border: 0;
    background: none;
    color: inherit;
    text-align: left;
    padding: 8px 4px 8px 12px;
    border-radius: var(--r-s);
    min-height: 60px;
  }
  .main:hover { background: color-mix(in srgb, var(--on-surface) 6%, transparent); }
  .logo {
    width: 40px;
    height: 40px;
    border-radius: var(--r-xs);
    background: var(--surface-high);
    flex: none;
    overflow: hidden;
  }
  .logo img { width: 100%; height: 100%; object-fit: cover; display: block; }
  .txt { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .t, .s { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .t { font-size: 16px; line-height: 22px; }
  .s { font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
  .pager { display: flex; justify-content: center; align-items: center; gap: 8px; padding: 12px 0; color: var(--on-surface-var); }
  .nums { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
</style>
