<script>
  // A modal bottom sheet (M3), as the mock-up draws the Player's «Звук»: a 50 % scrim, the sheet
  // from the bottom edge with a drag handle, a title row with a close button, radius 28 on top.
  // Wider than a phone it keeps a phone's width, centred. Esc, the scrim and the close button
  // dismiss it (`close`). docs/PLAN_SOUND_UX_2026-10-14.md, S3.
  import { createEventDispatcher, onMount, tick } from 'svelte';
  import { fade, fly } from 'svelte/transition';
  import { t } from '../stores/i18n.js';
  import { trap } from '../lib/trap.js';
  import Icon from './Icon.svelte';

  export let title = '';

  const dispatch = createEventDispatcher();
  const id = `sheet-${Math.random().toString(36).slice(2, 8)}`;
  let sheet;
  const close = () => dispatch('close');
  onMount(async () => {
    await tick();
    sheet?.focus();
  });
</script>

<svelte:window on:keydown={(e) => e.key === 'Escape' && close()} />
<div class="scrim" transition:fade={{ duration: 150 }} on:click|self={close} role="presentation">
  <section class="sheet" bind:this={sheet} role="dialog" aria-modal="true" aria-labelledby={id} tabindex="-1"
           use:trap transition:fly={{ y: 320, duration: 200 }}>
    <div class="handle" aria-hidden="true"></div>
    <div class="head">
      <h2 {id}>{title}</h2>
      <button class="x" type="button" aria-label={$t['common.close']} on:click={close}><Icon name="close" /></button>
    </div>
    <div class="body"><slot /></div>
  </section>
</div>

<style>
  .scrim {
    position: fixed;
    inset: 0;
    z-index: 1000;
    background: rgb(0 0 0 / 50%);
    display: flex;
    align-items: flex-end;
    justify-content: center;
  }
  .sheet {
    width: 100%;
    max-width: 560px;
    max-height: calc(100vh - 48px);
    max-height: calc(100dvh - 48px);      /* the toolbar of a phone's browser is not under it */
    overflow-y: auto;
    border-radius: var(--r-xl) var(--r-xl) 0 0;
    background: var(--surface-container);
    color: var(--on-surface);
    padding: 10px 24px calc(24px + var(--safe-b, 0px));   /* «Готово» above the home indicator */
    box-shadow: 0 -10px 30px rgb(0 0 0 / 45%);
    display: flex;
    flex-direction: column;
  }
  .sheet:focus { outline: none; }
  .handle {
    width: 36px;
    height: 4px;
    border-radius: 2px;
    background: var(--outline-var);
    align-self: center;
    flex: none;
  }
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    min-height: 56px;
    margin-top: 4px;
  }
  h2 { font-size: 22px; line-height: 28px; font-weight: 500; }
  .x {
    width: 44px;
    height: 44px;
    margin-right: -10px;
    border: 0;
    border-radius: 22px;
    background: none;
    color: var(--on-surface-var);
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .x:hover { background: color-mix(in srgb, currentColor 8%, transparent); }
</style>
