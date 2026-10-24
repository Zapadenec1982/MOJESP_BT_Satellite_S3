<script>
  // A picture of the panel's glass (GET /api/screenshot, a BMP, behind the login): what the panel
  // shows right now, to look at or to keep. Never with screen=: that switches the panel's page.
  // The picture is fetched rather than linked: an <img> cannot send the login.
  import { onDestroy } from 'svelte';
  import { t } from '../../stores/i18n.js';
  import { apiBlob } from '../../lib/api.js';
  import { toastError } from '../../stores/toast.js';
  import Button from '../../m3/Button.svelte';

  let url = '';
  let taken = null;
  let busy = false;

  async function take() {
    busy = true;
    try {
      const blob = await apiBlob('/api/screenshot');
      if (url) URL.revokeObjectURL(url);
      url = URL.createObjectURL(blob);
      taken = new Date();
    } catch (e) {
      if (e.message !== 'Unauthorized') toastError(e.message);
    } finally {
      busy = false;
    }
  }
  onDestroy(() => url && URL.revokeObjectURL(url));

  const stamp = (d) => d.toISOString().slice(0, 19).replace(/[:T]/g, '-');
</script>

<div class="acts">
  <Button icon="screenshot_monitor" disabled={busy} on:click={take}>{busy ? $t['shot.taking'] : url ? $t['shot.again'] : $t['shot.take']}</Button>
  {#if url}
    <Button variant="tonal" icon="download" href={url} on:click={(e) => e.currentTarget.setAttribute('download', `p4-screen-${stamp(taken)}.bmp`)}>{$t['shot.save']}</Button>
  {/if}
</div>
{#if url}
  <img class="shot" src={url} alt={$t['shot.alt']} />
{/if}

<style>
  .acts { display: flex; flex-wrap: wrap; gap: 8px; }
  .shot {
    display: block;
    margin-top: 16px;
    max-width: 100%;
    max-height: 70vh;
    border-radius: var(--r-s);
    background: #000;
  }
</style>
