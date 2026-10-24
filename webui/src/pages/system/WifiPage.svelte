<script>
  // Wi-Fi (PLAN_WEBUI §2, System › Links): the panel's Wi-Fi page (screen_wifi.cpp) — the network
  // it is on, the networks it hears, joining one, forgetting it — plus the access point it falls
  // back to (GET / POST /api/wifi/ap, applied on the next start).
  //
  // Joining is WiFiService::try_connect(), as on the glass (POST /api/wifi {"join":true}): the
  // stored network is kept until the new one gives an address and comes back if it does not.
  // The radio does leave the current network for the seconds the try takes, so this page drops
  // off and the WebSocket brings the outcome (wifi.try) when it is back. wifi.rssi is not
  // written by the firmware (the RPC is off, wifi_service.cpp), so the signal shown is the scan's.
  //
  // The firmware's JSON reader (jsmn) does not unescape strings: a " or a \ would reach the
  // panel with its backslash. Such names and passwords are stopped here, not sent wrong.
  import { onMount } from 'svelte';
  import { t, K, fmt } from '../../stores/i18n.js';
  import { state } from '../../stores/state.js';
  import { apiGet, apiPost } from '../../lib/api.js';
  import { toastSuccess, toastError } from '../../stores/toast.js';
  import Group from '../../m3/Group.svelte';
  import Row from '../../m3/Row.svelte';
  import Switch from '../../m3/Switch.svelte';
  import Button from '../../m3/Button.svelte';
  import IconButton from '../../m3/IconButton.svelte';
  import Dialog from '../../m3/Dialog.svelte';
  import TextField from '../../m3/TextField.svelte';
  import Icon from '../../m3/Icon.svelte';

  const AP_IP = '192.168.4.1';     // wifi_service.cpp start_ap(): always
  // What the panel joins (screen_wifi.cpp supported()); everything else the scan names is shown and
  // refused, as on the glass. WEP and WPA are old; the rest (Enterprise, WAPI, OWE…) unsupported.
  const OLD = ['WEP', 'WPA'];
  const JOIN = ['open', 'WPA2', 'WPA/WPA2', 'WPA3', 'WPA2/WPA3'];
  const joinable = (a) => JOIN.includes(a);
  const bytes = (x) => new TextEncoder().encode(x).length;
  // the panel's signal_icon() steps
  const level = (rssi) => (rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1);
  const authText = (a, tt) =>
    a === 'open' ? tt[K('wifi.open')] : OLD.includes(a) ? fmt(tt[K('wifi.old')], { auth: a })
    : JOIN.includes(a) ? a : a && a !== 'unknown' ? fmt(tt[K('wifi.unsupported')], { auth: a }) : tt[K('wifi.other')];

  // What the last refusal means (screen_wifi.cpp reason_text(); codes from esp_wifi_types_generic.h, IDF v5.5.5).
  function why(code, tt) {
    switch (code) {
      case 2: case 15: case 202: case 204: case 14: return tt[K('wifi.why_pass')];   // AUTH_EXPIRE, 4WAY_HANDSHAKE_TIMEOUT, AUTH_FAIL, HANDSHAKE_TIMEOUT, MIC_FAILURE
      case 201: return tt[K('wifi.why_none')];                                         // NO_AP_FOUND
      case 210: case 211: return tt[K('wifi.why_security')];                           // NO_AP_FOUND_W_COMPATIBLE_SECURITY, _IN_AUTHMODE_THRESHOLD
      case 0: return tt[K('wifi.why_silent')];
      default: return tt[K('wifi.why_refused')];
    }
  }

  $: s = $state;
  $: mode = s['wifi.mode'];
  $: trying = s['wifi.try'] === 'connecting';
  $: cur = s['wifi.connected'] && mode === 'sta' ? s['wifi.ssid'] : null;
  $: host = s['system.mdns_hostname'] || 'modesp';

  // ── the try's outcome: a change seen on this page is news, the value found on opening is not ──
  let seenTry;
  function noteTry(v) {
    if (v === undefined) return;
    if (seenTry === undefined || v === seenTry) { seenTry = v; return; }
    seenTry = v;
    const name = s['wifi.try_ssid'] || '';
    if (v === 'ok') toastSuccess(fmt($t['wifi.ok'], { name }));
    else if (v === 'fail') toastError(fmt($t['wifi.fail'], { name, why: why(s['wifi.reason'] ?? 0, $t) }));
  }
  $: tryNow = s['wifi.try'];
  $: noteTry(tryNow);

  // ── the networks nearby: looked for each time the page opens, one row per name, strongest first ──
  let nets = [];
  let scan = 'idle';      // busy | done | failed
  let mounted = false;
  async function rescan() {
    if (scan === 'busy' || trying) return;
    scan = 'busy';
    try {
      const d = await apiGet('/api/wifi/scan');
      if (!d.ok) throw new Error('scan');
      const best = new Map();
      for (const n of d.networks || []) {
        if (n.ssid && !(best.get(n.ssid)?.rssi >= n.rssi)) best.set(n.ssid, n);
      }
      nets = [...best.values()].sort((a, b) => b.rssi - a.rssi);
      scan = 'done';
    } catch (e) {
      scan = 'failed';
    }
  }
  $: if (mounted && scan === 'idle' && !trying) rescan();
  $: here = nets.find((n) => n.ssid === cur);

  // ── the card ──
  $: card = trying
    ? { icon: 'wifi', title: s['wifi.try_ssid'] || '…', sub: $t['wifi.connecting'] }
    : cur
      ? { icon: 'wifi', title: cur, sub: `${$t['wifi.connected']} · ${s['wifi.ip'] || $t['wifi.no_ip']}` }
      : mode === 'ap'
        ? { icon: 'wifi_off', title: fmt($t['wifi.ap_title'], { name: s['wifi.ssid'] || '' }), sub: `${s['wifi.ip'] || AP_IP} · ${$t['wifi.ap_sub']}` }
        : { icon: 'wifi_off', title: s['wifi.ssid'] || $t['wifi.not_connected'], sub: $t['wifi.retrying'] };
  // A stored network is what STA mode runs on. In AP mode the web cannot tell one is stored
  // (the panel asks WiFiService::has_credentials()), so there is nothing to forget here.
  $: canForget = mode === 'sta' && !trying;

  // ── joining ──
  let joining = null;
  let pass = '';
  let shown = false;
  function pick(n) {
    if (trying) return;
    joining = n;
    pass = '';
    shown = false;
  }
  $: secure = !!joining && joining.auth !== 'open';
  $: passOk = !secure || (pass.length >= 8 && bytes(pass) <= 64);
  async function join() {
    if (!passOk) return;
    const name = joining.ssid;
    const body = { ssid: name, password: secure ? pass : '', join: true };
    joining = null;
    pass = '';
    try {
      await apiPost('/api/wifi', body);
      seenTry = 'connecting';          // so an outcome that lands before 'connecting' is still news
      toastSuccess(fmt($t['wifi.joining'], { name }));
    } catch (e) {
      let busy = false;
      try { busy = JSON.parse(e.message).error === 'busy'; } catch (x) { /* plain text */ }
      toastError(busy ? $t['wifi.busy'] : e.message);
    }
  }

  // ── forgetting ──
  let forgetting = false;
  async function forget() {
    forgetting = false;
    try {
      await apiPost('/api/wifi', { forget: true });
      toastSuccess($t['wifi.forgetting']);
    } catch (e) {
      toastError(e.message);
    }
  }

  // ── the panel's access point ──
  let ap = null;
  let apOpen = false;
  let apName = '';
  let apLock = true;
  let apPass = '';
  let apShown = false;
  let apCh = 1;
  let apBusy = false;
  async function loadAp() {
    try {
      ap = await apiGet('/api/wifi/ap');
      apName = ap.ssid || '';
      apLock = !!ap.password_set;
      apCh = ap.channel || 1;
      apPass = '';
    } catch (e) {
      toastError(e.message);
    }
  }
  $: apOk = apName.trim().length > 0 && bytes(apName.trim()) <= 32 &&
            (!apLock || (apPass.length >= 8 && bytes(apPass) <= 63));
  async function saveAp() {
    if (!apOk) return;
    apBusy = true;
    try {
      // the handler stores what it is sent: no password means an open access point
      await apiPost('/api/wifi/ap', { ssid: apName.trim(), password: apLock ? apPass : '', channel: Number(apCh) });
      toastSuccess($t['wifi.ap_saved']);
      await loadAp();
    } catch (e) {
      toastError(e.message);
    } finally {
      apBusy = false;
    }
  }
  $: apSum = ap ? `${fmt($t['wifi.ap_sum'], { name: ap.ssid, ch: ap.channel })} · ${ap.password_set ? $t['wifi.ap_locked'] : $t['wifi.ap_open']}` : '';

  onMount(() => {
    mounted = true;
    loadAp();
  });
</script>

<Group>
  <Row icon={card.icon} title={card.title} subtitle={card.sub}>
    {#if here && !trying}
      <span class="bars" role="img" aria-label={fmt($t['wifi.signal'], { rssi: here.rssi })} title={fmt($t['wifi.signal'], { rssi: here.rssi })}>
        {#each [1, 2, 3, 4] as i}<i class:on={i <= level(here.rssi)} style="height:{3 + i * 3}px"></i>{/each}
      </span>
    {/if}
    {#if canForget}<Button variant="tonal" on:click={() => (forgetting = true)}>{$t['wifi.forget']}</Button>{/if}
  </Row>
</Group>

<div class="bar">
  <h2 class="cap">{$t['wifi.nearby']}</h2>
  <span class="st">{scan === 'busy' ? $t['wifi.scanning'] : scan === 'failed' ? $t['wifi.scan_failed'] : ''}</span>
  <IconButton icon="refresh" label={$t['wifi.scan']} disabled={scan === 'busy' || trying} on:click={rescan} />
</div>
{#if nets.length || scan === 'done'}
  <Group>
    {#each nets as n (n.ssid)}
      <Row list button={n.ssid !== cur} selected={n.ssid === cur} title={n.ssid}
           subtitle={n.ssid === cur ? $t['wifi.connected'] : authText(n.auth, $t)}
           disabled={n.ssid !== cur && (!joinable(n.auth) || trying)} on:click={() => pick(n)}>
        {#if n.auth !== 'open'}<span class="lock"><Icon name="lock" size={20} /></span>{/if}
        <span class="bars" role="img" aria-label={fmt($t['wifi.signal'], { rssi: n.rssi })} title={fmt($t['wifi.signal'], { rssi: n.rssi })}>
          {#each [1, 2, 3, 4] as i}<i class:on={i <= level(n.rssi)} style="height:{3 + i * 3}px"></i>{/each}
        </span>
      </Row>
    {:else}
      <p class="s pad">{$t['wifi.none']}</p>
    {/each}
  </Group>
{/if}

<div class="sp"></div>
<Group>
  <Row button icon="hub" title={$t['sys.wifi_ap']} subtitle={apSum} on:click={() => (apOpen = !apOpen)}>
    <span class="caret" class:up={apOpen}><Icon name="arrow_drop_down" /></span>
  </Row>
  {#if apOpen && ap}
    <div class="form">
      <TextField label={$t['wifi.ap_name']} bind:value={apName} maxlength={32} />
      <div class="line">
        <span class="txt">
          <span class="t">{$t['wifi.ap_lock']}</span>
          <span class="s0">{apLock ? $t['wifi.pass_rule'] : $t['wifi.ap_lock_off']}</span>
        </span>
        <Switch checked={apLock} label={$t['wifi.ap_lock']} on:change={(e) => (apLock = e.detail)} />
      </div>
      {#if apLock}
        <div class="pw">
          <TextField label={$t['wifi.password']} type={apShown ? 'text' : 'password'} bind:value={apPass}
                     autocomplete="new-password" maxlength={63} />
          <IconButton icon={apShown ? 'visibility_off' : 'visibility'} label={apShown ? $t['wifi.hide'] : $t['wifi.show']}
                      on:click={() => (apShown = !apShown)} />
        </div>
        {#if ap.password_set}<p class="s0">{$t['wifi.ap_pass_again']}</p>{/if}
      {/if}
      <div class="line">
        <span class="t">{$t['wifi.ap_channel']}</span>
        <select class="pick" aria-label={$t['wifi.ap_channel']} bind:value={apCh}>
          {#each Array.from({ length: 13 }, (_, i) => i + 1) as c}<option value={c}>{c}</option>{/each}
        </select>
      </div>
      <p class="s0">{$t['wifi.ap_note']}</p>
      <div class="end">
        <Button disabled={apBusy || !apOk} on:click={saveAp}>{$t['wifi.save']}</Button>
      </div>
    </div>
  {/if}
</Group>

{#if joining}
  <Dialog title={fmt(secure ? $t['wifi.pass_q'] : $t['wifi.join_q'], { name: joining.ssid })} on:close={() => (joining = null)}>
    {#if secure}
      <div class="pw">
        <TextField label={$t['wifi.password']} type={shown ? 'text' : 'password'} bind:value={pass}
                   maxlength={64}
                   on:keydown={(e) => e.key === 'Enter' && join()} />
        <IconButton icon={shown ? 'visibility_off' : 'visibility'} label={shown ? $t['wifi.hide'] : $t['wifi.show']}
                    on:click={() => (shown = !shown)} />
      </div>
      <p class="s0 gap">{$t['wifi.pass_rule']}</p>
    {/if}
    <p class="note">{fmt(mode === 'ap' ? $t['wifi.join_note_ap'] : $t['wifi.join_note'], { name: joining.ssid, host })}</p>
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (joining = null)}>{$t['sys.cancel']}</Button>
      <Button disabled={!passOk} on:click={join}>{$t['wifi.join']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

{#if forgetting}
  <Dialog title={fmt($t['wifi.forget_q'], { name: s['wifi.ssid'] || '' })} on:close={() => (forgetting = false)}>
    <p class="b">{$t['wifi.forget_body']}</p>
    {#if ap}<p class="b gap">{fmt($t['wifi.forget_back'], { name: ap.ssid, ip: AP_IP })}</p>{/if}
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (forgetting = false)}>{$t['sys.cancel']}</Button>
      <Button danger on:click={forget}>{$t['wifi.forget']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .bar { display: flex; align-items: center; gap: 8px; padding: 8px 4px 0 16px; }
  .cap { font-size: 14px; line-height: 20px; font-weight: 500; color: var(--on-surface-var); flex: 1; }
  .st { font-size: 14px; color: var(--on-surface-var); }
  .s { font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
  .pad { padding: 16px; }
  .lock { color: var(--on-surface-var); display: flex; flex: none; }
  .bars { display: inline-flex; align-items: flex-end; gap: 2px; height: 16px; flex: none; margin-right: 8px; }
  .bars i { width: 4px; border-radius: 1px; background: var(--outline-var); }
  .bars i.on { background: var(--on-surface-var); }
  .caret { color: var(--on-surface-var); display: flex; flex: none; transition: transform 150ms ease; }
  .caret.up { transform: rotate(180deg); }
  .form { display: flex; flex-direction: column; gap: 12px; padding: 8px 16px 12px; }
  .line { display: flex; align-items: center; gap: 12px; }
  .txt { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .t { font-size: 16px; line-height: 22px; color: var(--on-surface); flex: 1; }
  .s0 { font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
  .pw { display: flex; align-items: flex-end; gap: 4px; }
  .pw > :global(:first-child) { flex: 1; }
  .note { font-size: 14px; line-height: 20px; color: var(--on-surface-var); margin-top: 12px; }
  .gap { margin-top: 8px; }
  .sp { height: 16px; }
  .b { font-size: 16px; line-height: 24px; color: var(--on-surface-var); }
  .end { display: flex; justify-content: flex-end; }
  .pick {
    height: 40px;
    border: 1px solid var(--outline-var);
    border-radius: 20px;
    padding: 0 12px;
    background: var(--secondary-container);
    color: var(--on-secondary-container);
    font: inherit;
    font-size: 15px;
  }
  .pick option { background: var(--surface-container); color: var(--on-surface); }
</style>
