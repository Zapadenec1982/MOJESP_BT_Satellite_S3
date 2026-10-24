<script>
  // Snackbars for stores/toast.js, drawn like the panel's toast (ui_m3_widgets.cpp:832-856):
  // inverse_surface, radius 8, 48 px, bottom centre. Raised above the navigation bar and the
  // mini-player on a phone.
  import { fly, fade } from 'svelte/transition';
  import { toasts, dismissToast } from '../stores/toast.js';
  import { t } from '../stores/i18n.js';
  import Icon from './Icon.svelte';
</script>

<div class="host" aria-live="polite">
  {#each $toasts as toast (toast.id)}
    <div class="snack {toast.type}" in:fly={{ y: 24, duration: 180 }} out:fade={{ duration: 120 }}>
      {#if toast.type === 'error'}<Icon name="error" size={20} />{:else if toast.type === 'warn'}<Icon name="warning" size={20} />{/if}
      <span class="msg">{toast.msg}</span>
      {#if toast.action}
        <button class="act" type="button" on:click={() => { dismissToast(toast.id); toast.action.run(); }}>{toast.action.label}</button>
      {/if}
      <button class="x" type="button" aria-label={$t['common.close']} on:click={() => dismissToast(toast.id)}><Icon name="close" size={20} /></button>
    </div>
  {/each}
</div>

<style>
  .host {
    position: fixed;
    left: 50%;
    transform: translateX(-50%);
    bottom: calc(var(--navbar-h) + var(--mini-h) + 16px);
    /* over the sheets and dialogs (1000): «Скасувати» for a move made in the «Звук» sheet, and an
       error raised in a band's sheet, were under them, out of reach until they expired */
    z-index: 1100;
    width: min(560px, calc(100% - 2 * var(--gutter)));
    display: flex;
    flex-direction: column;
    gap: 8px;
    pointer-events: none;
  }
  @media (min-width: 600px) {
    .host { bottom: calc(var(--mini-h) + 24px); left: calc(50% + var(--rail-w) / 2); }
  }
  .snack {
    pointer-events: auto;
    min-height: 48px;
    border-radius: var(--r-xs);
    background: var(--inverse-surface);
    color: var(--inverse-on-surface);
    box-shadow: 0 6px 20px rgb(0 0 0 / 40%);
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 4px 6px 16px;
    font-size: 15px;
    line-height: 20px;
  }
  .error :global(.ms) { color: var(--error); }
  .msg { flex: 1; min-width: 0; }
  .act {
    height: 40px;
    padding: 0 12px;
    border: 0;
    border-radius: 20px;
    background: none;
    color: var(--inverse-primary, var(--primary));
    font: inherit;
    font-weight: 500;
  }
  .act:hover { background: color-mix(in srgb, currentColor 8%, transparent); }
  .x {
    width: 40px;
    height: 40px;
    border: 0;
    border-radius: 20px;
    background: none;
    color: inherit;
    display: flex;
    align-items: center;
    justify-content: center;
    flex: none;
  }
</style>
