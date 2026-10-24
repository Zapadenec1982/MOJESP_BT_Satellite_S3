<script>
  // Firmware (PLAN_WEBUI §2, System › Device): what runs now (GET /api/ota) and an update from a
  // .bin file (POST /api/ota). The image goes into the other app slot and the panel restarts
  // into it; the web interface itself is in the data partition and is not part of the image.
  import { onMount, onDestroy } from 'svelte';
  import { t, fmt } from '../../stores/i18n.js';
  import { apiGet, apiUpload } from '../../lib/api.js';
  import { toastError, toastWarn } from '../../stores/toast.js';
  import Group from '../../m3/Group.svelte';
  import Row from '../../m3/Row.svelte';
  import Button from '../../m3/Button.svelte';
  import Dialog from '../../m3/Dialog.svelte';

  // The app slots are 4 MB (ota_0 / ota_1, partitions_p4_16mb.csv); a bigger image is refused.
  const SLOT = 4 * 1024 * 1024;

  let info = null;
  let file = null;
  let input;
  let asking = false;
  let pct = -1;
  let left = 0;
  let timer;

  onMount(async () => {
    try { info = await apiGet('/api/ota'); } catch (e) { /* shown as unknown */ }
  });
  onDestroy(() => clearInterval(timer));

  const size = (b) => (b < 1048576 ? `${(b / 1024).toFixed(0)} KB` : `${(b / 1048576).toFixed(2)} MB`);

  function pick(e) {
    const f = e.target.files[0];
    file = null;
    if (!f) return;
    if (!f.name.endsWith('.bin')) { toastWarn($t['fw.only_bin']); input.value = ''; return; }
    if (f.size > SLOT) { toastWarn(fmt($t['fw.too_big'], { size: size(f.size) })); input.value = ''; return; }
    file = f;
  }

  async function upload() {
    asking = false;
    pct = 0;
    try {
      await apiUpload('/api/ota', file, (p) => (pct = p));
      pct = 100;
      left = 10;
      timer = setInterval(() => {
        left--;
        if (left <= 0) { clearInterval(timer); location.reload(); }
      }, 1000);
    } catch (e) {
      pct = -1;
      let msg = e.message;
      try {
        const r = JSON.parse(msg);
        if (r.error === 'board_mismatch') msg = fmt($t['fw.board_mismatch'], { incoming: r.incoming, running: r.running });
      } catch (x) { /* plain text */ }
      toastError(msg);
    }
  }
</script>

<Group caption={$t['fw.running']}>
  <Row icon="memory" title={info ? info.version : '…'} subtitle={info ? `${info.date} ${info.time}` : ''} />
  {#if info}
    <Row icon="developer_board" title={info.board} subtitle={`${$t['fw.slot']} ${info.partition} · IDF ${info.idf}`} />
  {/if}
</Group>

<Group caption={$t['fw.update']}>
  <div class="up">
    <input bind:this={input} type="file" accept=".bin" on:change={pick} disabled={pct >= 0} />
    {#if file}<p class="s">{file.name} · {size(file.size)}</p>{/if}
    {#if pct >= 0}
      <div class="bar" role="progressbar" aria-valuenow={pct} aria-valuemin="0" aria-valuemax="100">
        <span style="width:{pct}%"></span>
      </div>
      <p class="s">{pct < 100 ? `${pct} %` : fmt($t['fw.restarting'], { n: left })}</p>
    {/if}
    <div class="end">
      <Button icon="upload_file" disabled={!file || pct >= 0} on:click={() => (asking = true)}>{$t['fw.install']}</Button>
    </div>
  </div>
  <p class="s note">{$t['fw.note']}</p>
</Group>

{#if asking}
  <Dialog title={$t['fw.confirm_q']} on:close={() => (asking = false)}>
    <p class="b">{$t['fw.confirm_body']}</p>
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (asking = false)}>{$t['sys.cancel']}</Button>
      <Button on:click={upload}>{$t['fw.install']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .up { padding: 12px 16px; display: flex; flex-direction: column; gap: 10px; }
  .up input { color: var(--on-surface-var); font: inherit; }
  .s { font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
  .note { padding: 0 16px 12px; }
  .bar { height: 8px; border-radius: 4px; background: var(--surface-highest); overflow: hidden; }
  .bar span { display: block; height: 100%; background: var(--primary); transition: width 200ms; }
  .end { display: flex; justify-content: flex-end; }
  .b { font-size: 16px; line-height: 24px; color: var(--on-surface-var); }
</style>
