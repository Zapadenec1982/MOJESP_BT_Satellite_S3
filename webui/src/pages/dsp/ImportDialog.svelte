<script>
  // «Імпорт фільтрів»: Equalizer APO or REW text, pasted or
  // from a file, read as it is typed — what will come in and what will not, said before anything
  // is sent. `import` with the bands; the page makes them «Власний».
  import { createEventDispatcher } from 'svelte';
  import { t, K, fmt, plural, language } from '../../stores/i18n.js';
  import { parseFilters } from '../../lib/eqfile.js';
  import Dialog from '../../m3/Dialog.svelte';
  import Button from '../../m3/Button.svelte';

  const dispatch = createEventDispatcher();
  K('eq.import_n.one'); K('eq.import_n.few'); K('eq.import_n.many'); K('eq.import_n.other');

  let text = '';
  let file;
  let readError = '';

  $: p = parseFilters(text);
  $: notes = [
    p.preamp !== null && p.preamp !== 0 ? fmt($t['eq.import_preamp'], { db: p.preamp }) : '',
    p.unsupported ? fmt($t['eq.import_unsupported'], { n: p.unsupported }) : '',
    p.unreadable ? fmt($t['eq.import_unreadable'], { n: p.unreadable }) : '',
    p.extra ? fmt($t['eq.import_extra'], { n: p.extra }) : '',
    p.clamped ? fmt($t['eq.import_clamped'], { n: p.clamped }) : '',
  ].filter(Boolean);

  async function pick(e) {
    const f = e.target.files && e.target.files[0];
    if (!f) return;
    readError = '';
    try {
      text = await f.text();
    } catch (err) {
      readError = err.message || String(err);
    }
    e.target.value = '';
  }
</script>

<Dialog title={$t['eq.import']} on:close>
  <p class="hint">{$t['eq.import_hint']}</p>
  <textarea bind:value={text} rows="7" spellcheck="false" aria-label={$t['eq.import']}
            placeholder="Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 1.41"></textarea>
  <input bind:this={file} class="file" type="file" accept=".txt,text/plain" on:change={pick} />
  <Button variant="text" icon="upload_file" on:click={() => file.click()}>{$t['eq.import_file']}</Button>
  <div class="sum" role="status">
    {#if readError}
      <p class="err">{readError}</p>
    {:else if text.trim()}
      <p class:none={!p.bands.length}>{p.bands.length ? plural($t, $language, 'eq.import_n', p.bands.length) : $t['eq.import_none']}</p>
      {#each notes as line}<p class="note">{line}</p>{/each}
    {/if}
  </div>
  <svelte:fragment slot="actions">
    <Button variant="text" on:click={() => dispatch('close')}>{$t['common.cancel']}</Button>
    <Button disabled={!p.bands.length} on:click={() => dispatch('import', p.bands)}>{$t['eq.import_go']}</Button>
  </svelte:fragment>
</Dialog>

<style>
  .hint { font-size: 14px; line-height: 20px; color: var(--on-surface-var); margin-bottom: 10px; }
  textarea {
    width: 100%;
    min-height: 140px;
    resize: vertical;
    border: 1px solid var(--outline);
    border-radius: var(--r-xs);
    background: var(--surface-low);
    color: var(--on-surface);
    font-family: var(--font-mono);
    font-size: 13px;
    line-height: 18px;
    padding: 8px 10px;
  }
  textarea:focus { outline: 2px solid var(--primary); outline-offset: -1px; }
  .file { display: none; }
  .sum { min-height: 22px; margin-top: 6px; font-size: 14px; line-height: 20px; }
  .sum .none, .err { color: var(--error); }
  .note { color: var(--on-surface-var); }
</style>
