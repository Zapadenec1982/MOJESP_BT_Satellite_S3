<script>
  // What is playing, small, over the navigation: the source's glyph, the title and the line
  // under it, and play/pause. A tap on the text opens the Player.
  import { now, subLine } from '../stores/now.js';
  import { t, language } from '../stores/i18n.js';
  import { href } from '../stores/router.js';
  import { command } from '../lib/cmd.js';
  import Icon from '../m3/Icon.svelte';
  import IconButton from '../m3/IconButton.svelte';

  $: np = $now;
  $: title = np.title ?? $t[np.titleKey];
  $: sub = subLine(np, $t, $language);
  $: toggleIcon = np.playing ? (np.stopIcon ? 'stop' : 'pause') : 'play_arrow';
  $: toggleLabel = !np.playing ? $t['transport.play'] : np.stopIcon ? $t['transport.stop'] : $t['transport.pause'];
</script>

<div class="mini">
  <a class="open" href={href('player')} aria-label={$t['mini.open']}>
    <span class="art"><Icon name={np.icon} /></span>
    <span class="txt">
      <span class="t">{title}</span>
      {#if sub}<span class="s">{sub}</span>{/if}
    </span>
  </a>
  {#if np.toggle && np.live}
    <IconButton icon={toggleIcon} label={toggleLabel} on:click={() => command(np.toggle.key, np.toggle.value)} />
  {/if}
</div>

<style>
  .mini {
    height: var(--mini-h);
    border-radius: var(--r-s);
    background: var(--surface-high);
    display: flex;
    align-items: center;
    gap: 4px;
    padding: 0 4px 0 8px;
    box-shadow: 0 4px 16px rgb(0 0 0 / 30%);
  }
  .open {
    flex: 1;
    min-width: 0;
    display: flex;
    align-items: center;
    gap: 12px;
    color: inherit;
    text-decoration: none;
    height: 100%;
  }
  .art {
    width: 48px;
    height: 48px;
    border-radius: var(--r-xs);
    background: var(--surface-highest);
    color: var(--outline);
    display: flex;
    align-items: center;
    justify-content: center;
    flex: none;
  }
  .txt { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .t, .s { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .t { font-size: 15px; line-height: 20px; font-weight: 500; }
  .s { font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
</style>
