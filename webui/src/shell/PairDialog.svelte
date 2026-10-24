<script>
  // A phone asks to pair (PLAN_WEBUI decision 6): the question is on the panel and over any page
  // of every open tab at once, with the panel's own words and code (ui_shell.cpp, pairing). The
  // first answer wins on the device; the place that was second says where it was answered.
  //
  // bt.pair_* in the state says a request is waiting. The code comes from GET /api/bt, behind
  // the login — so a visitor who is not signed in sees the question and answers on the panel.
  import { onDestroy } from 'svelte';
  import { state } from '../stores/state.js';
  import { t, fmt } from '../stores/i18n.js';
  import { apiGet } from '../lib/api.js';
  import { loadBt, sendBt, codeText, PAIR_NUMERIC, PAIR_CONSENT, PAIR_PASSKEY, BY_PANEL } from '../lib/bt.js';
  import Dialog from '../m3/Dialog.svelte';
  import Button from '../m3/Button.svelte';

  const RESULT_MS = 2500;      // the panel's PAIR_RESULT_MS

  let open = false;
  let shown = 0;               // the request the dialog is about
  let code = null;
  let canAnswer = false;
  let mine = false;            // this tab answered
  let note = '';               // instead of the question once it is answered
  let done = false;
  let closer;

  $: s = $state;
  $: seq = s['bt.pair_seq'] || 0;
  $: active = !!s['bt.pair_active'];
  $: kind = s['bt.pair_kind'] ?? PAIR_NUMERIC;
  $: name = s['bt.pair_name'] || $t['pair.phone'];
  $: by = s['bt.pair_by'] || 0;
  $: result = s['bt.pair_result'] ?? 255;

  $: if (active && seq && seq !== shown) begin(seq);
  $: if (open && !done && !mine && by && seq === shown) note = by === BY_PANEL ? $t['pair.answered_panel'] : $t['pair.answered_tab'];
  $: if (open && !done && !active && seq === shown && result !== 255) finish(result);

  async function begin(n) {
    shown = n;
    open = true;
    done = false;
    mine = false;
    note = '';
    code = null;
    clearTimeout(closer);
    // Signed in, or no login at all: then this tab may answer, and read the code.
    let creds = null;
    try { creds = sessionStorage.getItem('modesp_auth'); } catch (e) { /* storage blocked */ }
    try {
      const a = await apiGet('/api/auth');
      canAnswer = !a.enabled || !!creds;
    } catch (e) {
      canAnswer = !!creds;
    }
    if (!canAnswer || kind === PAIR_CONSENT) return;
    try {
      const d = await loadBt();
      if (d.pairing && d.pairing.seq === n) code = d.pairing.code;
    } catch (e) { /* the question still stands without its code */ }
  }

  function finish(r) {
    done = true;
    note = [fmt($t['pair.ok'], { name }), $t['pair.rejected'], $t['pair.timeout']][r] || $t['pair.failed'];
    closer = setTimeout(() => (open = false), RESULT_MS);
  }

  async function answer(accept) {
    mine = true;
    note = accept ? $t['pair.confirm_phone'] : $t['pair.rejecting'];
    try {
      const r = await sendBt({ pair: accept ? 'accept' : 'reject', seq: shown });
      if (r.error === 'answered') note = r.by === BY_PANEL ? $t['pair.answered_panel'] : $t['pair.answered_tab'];
      else if (r.error) note = $t['pair.failed'];
    } catch (e) {
      note = e.message;
    }
  }

  onDestroy(() => clearTimeout(closer));

  $: question =
    kind === PAIR_PASSKEY ? fmt($t['pair.passkey'], { name })
    : kind === PAIR_CONSENT ? fmt($t['pair.consent'], { name })
    : fmt($t['pair.numeric'], { name });
</script>

{#if open}
  <Dialog title={done ? '' : $t['pair.title']} on:close={() => (open = false)}>
    {#if note}
      <p class="b">{note}</p>
    {:else}
      <p class="b">{question}</p>
      {#if kind !== PAIR_CONSENT}
        <p class="code" aria-live="polite">{code !== null ? codeText(code) : '··· ···'}</p>
      {/if}
      {#if !canAnswer && kind !== PAIR_PASSKEY}<p class="hint">{$t['pair.on_panel']}</p>{/if}
    {/if}
    <svelte:fragment slot="actions">
      {#if !note && canAnswer && kind !== PAIR_PASSKEY}
        <Button variant="text" on:click={() => answer(false)}>{$t['pair.reject']}</Button>
        <Button on:click={() => answer(true)}>{$t['pair.connect']}</Button>
      {:else}
        <Button variant="text" on:click={() => (open = false)}>{$t['pair.hide']}</Button>
      {/if}
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .b { font-size: 16px; line-height: 24px; color: var(--on-surface-var); }
  .code {
    font-size: 44px;
    line-height: 52px;
    font-weight: 400;
    letter-spacing: 2px;
    color: var(--primary);
    margin: 16px 0 4px;
    font-variant-numeric: tabular-nums;
  }
  .hint { font-size: 14px; color: var(--on-surface-var); margin-top: 8px; }
</style>
