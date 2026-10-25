<script>
  // Backup: two files a person keeps.
  //  - Their own files (A6, GET / POST /api/userdata): the station list, the EQ and «Власний», the
  //    remote devices, the track to resume. A restore is staged and goes in at the next boot,
  //    because the modules hold their own copies (main/userdata_http.cpp) — so it ends with a
  //    restart.
  //  - The settings (GET /api/backup, POST /api/restore): every persisted key, applied at once.
  import { t, fmt } from '../../stores/i18n.js';
  import { apiGet, apiPost } from '../../lib/api.js';
  import { toastSuccess, toastError } from '../../stores/toast.js';
  import Group from '../../m3/Group.svelte';
  import Button from '../../m3/Button.svelte';
  import Dialog from '../../m3/Dialog.svelte';

  let busy = '';
  let staged = 0;            // files waiting for the restart
  let restarting = false;
  let filesInput, settingsInput;

  const today = () => new Date().toISOString().slice(0, 10);

  function save(obj, name) {
    const url = URL.createObjectURL(new Blob([JSON.stringify(obj, null, 1)], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  async function download(what) {
    busy = what;
    try {
      if (what === 'files') save(await apiGet('/api/userdata'), `p4-files-${today()}.json`);
      else save(await apiGet('/api/backup'), `p4-settings-${today()}.json`);
    } catch (e) {
      toastError(e.message);
    } finally {
      busy = '';
    }
  }

  async function read(input) {
    const f = input.files[0];
    input.value = '';
    if (!f) return null;
    try {
      return JSON.parse(await f.text());
    } catch (e) {
      toastError($t['backup.not_json']);
      return null;
    }
  }

  async function restoreFiles() {
    const doc = await read(filesInput);
    if (!doc) return;
    if (!doc.modesp_userdata) { toastError($t['backup.wrong_kind']); return; }
    busy = 'files';
    try {
      const r = await apiPost('/api/userdata', doc);
      staged = r.staged || 0;
    } catch (e) {
      let msg = e.message;
      try {
        const r = JSON.parse(msg);
        if (r.error === 'bad_file') msg = fmt($t['backup.bad_file'], { file: r.file });
      } catch (x) { /* plain text */ }
      toastError(msg);
    } finally {
      busy = '';
    }
  }

  async function restoreSettings() {
    const doc = await read(settingsInput);
    if (!doc) return;
    if (!doc.modesp_backup) { toastError($t['backup.wrong_kind']); return; }
    busy = 'settings';
    try {
      await apiPost('/api/restore', doc);
      toastSuccess($t['backup.settings_done']);
    } catch (e) {
      toastError(e.message);
    } finally {
      busy = '';
    }
  }

  async function restart() {
    restarting = true;
    try {
      await apiPost('/api/restart', {});
      setTimeout(() => location.reload(), 15000);
    } catch (e) {
      restarting = false;
      toastError(e.message);
    }
  }
</script>

<Group caption={$t['backup.files']}>
  <p class="s">{$t['backup.files_sub']}</p>
  <div class="acts">
    <Button variant="tonal" icon="download" disabled={!!busy} on:click={() => download('files')}>{$t['backup.download']}</Button>
    <Button variant="text" icon="upload_file" disabled={!!busy} on:click={() => filesInput.click()}>{$t['backup.restore']}</Button>
  </div>
  <input bind:this={filesInput} type="file" accept=".json,application/json" hidden on:change={restoreFiles} />
</Group>

<Group caption={$t['backup.settings']}>
  <p class="s">{$t['backup.settings_sub']}</p>
  <div class="acts">
    <Button variant="tonal" icon="download" disabled={!!busy} on:click={() => download('settings')}>{$t['backup.download']}</Button>
    <Button variant="text" icon="upload_file" disabled={!!busy} on:click={() => settingsInput.click()}>{$t['backup.restore']}</Button>
  </div>
  <input bind:this={settingsInput} type="file" accept=".json,application/json" hidden on:change={restoreSettings} />
</Group>

{#if staged}
  <Dialog title={fmt($t['backup.staged'], { n: staged })} persistent>
    <p class="b">{restarting ? $t['sys.restarting'] : $t['backup.staged_body']}</p>
    <svelte:fragment slot="actions">
      {#if !restarting}
        <Button variant="text" on:click={() => (staged = 0)}>{$t['backup.later']}</Button>
        <Button on:click={restart}>{$t['sys.restart']}</Button>
      {/if}
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .s { font-size: 14px; line-height: 20px; color: var(--on-surface-var); padding: 12px 16px 4px; }
  .acts { display: flex; gap: 8px; flex-wrap: wrap; padding: 8px 12px 12px; }
  .b { font-size: 16px; line-height: 24px; color: var(--on-surface-var); }
</style>
