<script>
  // Bluetooth (PLAN_WEBUI §2, System › Links): what the panel's Bluetooth page shows and asks
  // (screen_bt.cpp) — the phone that is connected, the radio, visibility, auto-reconnect, the
  // name, the codecs and the phones the satellite remembers — over GET / POST /api/bt (A5).
  // bt.cfg_rev in the state says the satellite's settings moved; the page reads them again.
  import { onMount } from 'svelte';
  import { t, K, fmt } from '../../stores/i18n.js';
  import { state } from '../../stores/state.js';
  import { toastError } from '../../stores/toast.js';
  import { loadBt, sendBt, CODECS, NAME_MAX } from '../../lib/bt.js';
  import Group from '../../m3/Group.svelte';
  import Row from '../../m3/Row.svelte';
  import Switch from '../../m3/Switch.svelte';
  import Chip from '../../m3/Chip.svelte';
  import Dialog from '../../m3/Dialog.svelte';
  import Button from '../../m3/Button.svelte';
  import TextField from '../../m3/TextField.svelte';

  let v = null;
  let failed = '';
  let seen = -1;

  async function load() {
    try {
      v = await loadBt();
      failed = '';
    } catch (e) {
      failed = e.message;
    }
  }
  onMount(load);

  $: s = $state;
  $: rev = s['bt.cfg_rev'] ?? 0;
  $: if (v && rev !== seen) { seen = rev; load(); }

  const WHY = {
    no_satellite: K('bt.err_no_satellite'),
    old_satellite: K('bt.err_old'),
    not_known_yet: K('bt.err_wait'),
    name: K('bt.err_name'),
  };
  async function send(body) {
    try {
      const r = await sendBt(body);
      if (r.error) toastError($t[WHY[r.error]] || r.error);
    } catch (e) {
      toastError(e.message);
    }
  }

  const sw = (key, on) => {
    v = { ...v, [key]: on };          // as the device will say on the next bt.cfg_rev
    send({ [key]: on });
  };
  function codec(bit) {
    const next = v.codecs_enabled ^ bit;
    v = { ...v, codecs_enabled: next };
    send({ codecs: next | 1 });
  }

  // ── the name ──
  let naming = false;
  let draft = '';
  const bytes = (x) => new TextEncoder().encode(x).length;
  $: nameOk = draft.trim().length > 0 && bytes(draft.trim()) <= NAME_MAX;
  function saveName() {
    if (!nameOk) return;
    naming = false;
    send({ name: draft.trim() });
  }

  // ── forgetting ──
  let forgetting = null;     // a bond, or 'all'
  function forget() {
    const f = forgetting;
    forgetting = null;
    send({ forget: f === 'all' ? 'all' : f.addr });
  }

  $: settings = v && v.linked && v.settings;
  $: phone = s['bt.peer'];
</script>

{#if !v}
  <p class="msg">{failed || $t['app.loading']}</p>
{:else if !v.linked}
  <p class="msg">{$t['bt.no_satellite']}</p>
{:else}
  <Group>
    <Row icon="smartphone" title={phone || $t['bt.no_phone']} subtitle={phone ? s['bt.codec'] || $t['bt.connected'] : ''}>
      {#if phone}<Button variant="tonal" on:click={() => send({ disconnect: true })}>{$t['bt.disconnect']}</Button>{/if}
    </Row>
  </Group>

  {#if !v.settings}
    <p class="msg">{$t['bt.old_satellite']}</p>
  {:else}
    <Group caption={$t['bt.settings']}>
      <Row icon="bluetooth" title="Bluetooth" subtitle={!v.known ? '…' : v.radio ? $t['bt.on'] : $t['bt.off']}>
        <Switch checked={v.radio} label="Bluetooth" disabled={!v.known} on:change={(e) => sw('radio', e.detail)} />
      </Row>
      <Row icon="bluetooth_searching" title={$t['bt.visible']} subtitle={v.discoverable ? $t['bt.visible_on'] : $t['bt.visible_off']}>
        <Switch checked={v.discoverable} label={$t['bt.visible']} disabled={!v.known} on:change={(e) => sw('discoverable', e.detail)} />
      </Row>
      <Row icon="link" title={$t['bt.auto']}>
        <Switch checked={v.auto_reconnect} label={$t['bt.auto']} disabled={!v.known} on:change={(e) => sw('auto_reconnect', e.detail)} />
      </Row>
      <Row button icon="edit" title={$t['bt.name']} subtitle={v.name || '…'} disabled={!v.known}
           on:click={() => { draft = v.name || ''; naming = true; }} />
    </Group>

    <Group caption={$t['bt.codecs']}>
      <div class="chips">
        <Chip selected>SBC</Chip>
        {#each CODECS.filter((c) => v.codecs_supported & c.bit) as c (c.bit)}
          <Chip button selected={!!(v.codecs_enabled & c.bit)} on:click={() => codec(c.bit)}>{c.name}</Chip>
        {/each}
      </div>
    </Group>

    <Group caption={fmt($t['bt.phones'], { n: v.total })}>
      {#each v.bonds as b (b.addr)}
        <Row list icon="smartphone" title={b.name || b.addr}
             subtitle={b.connected ? $t['bt.connected'] : b.last ? $t['bt.last'] : b.addr} selected={b.connected}>
          <Button variant="text" on:click={() => (forgetting = b)}>{$t['bt.forget']}</Button>
        </Row>
      {:else}
        <p class="s pad">{$t['bt.no_phones']}</p>
      {/each}
      {#if v.bonds.length > 1}
        <div class="end"><Button variant="text" danger on:click={() => (forgetting = 'all')}>{$t['bt.forget_all']}</Button></div>
      {/if}
    </Group>
  {/if}
{/if}

{#if naming}
  <Dialog title={$t['bt.name_title']} on:close={() => (naming = false)}>
    <TextField label={$t['bt.name']} bind:value={draft} maxlength={NAME_MAX}
               on:keydown={(e) => e.key === 'Enter' && saveName()} />
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (naming = false)}>{$t['sys.cancel']}</Button>
      <Button disabled={!nameOk} on:click={saveName}>{$t['bt.save']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

{#if forgetting}
  <Dialog title={forgetting === 'all' ? $t['bt.forget_all_q'] : fmt($t['bt.forget_q'], { name: forgetting.name || forgetting.addr })}
          on:close={() => (forgetting = null)}>
    <p class="b">{$t['bt.forget_body']}</p>
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (forgetting = null)}>{$t['sys.cancel']}</Button>
      <Button danger on:click={forget}>{$t['bt.forget']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px; }
  .chips { display: flex; flex-wrap: wrap; gap: 8px; padding: 12px 12px 4px; }
  .s { font-size: 14px; line-height: 20px; color: var(--on-surface-var); padding: 4px 16px 12px; }
  .pad { padding: 16px; }
  .end { display: flex; justify-content: flex-end; padding: 4px 8px 8px; }
  .b { font-size: 16px; line-height: 24px; color: var(--on-surface-var); }
</style>
