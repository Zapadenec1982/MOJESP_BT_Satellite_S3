<script>
  // The four sections as M3 navigation items: an indicator pill behind the icon, the label under
  // it; the active one on secondary_container with the filled glyph, as in the panel's drawer.
  import { NAV } from '../lib/nav.js';
  import { href } from '../stores/router.js';
  import { t } from '../stores/i18n.js';
  import Icon from '../m3/Icon.svelte';

  export let active;
</script>

{#each NAV as item (item.id)}
  <a class="item" class:on={item.id === active} href={href(item.id)} aria-current={item.id === active ? 'page' : null}>
    <span class="ind"><Icon name={item.icon} fill={item.id === active} /></span>
    <span class="lbl">{$t[item.key]}</span>
  </a>
{/each}

<style>
  .item {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 4px;
    color: var(--on-surface-var);
    text-decoration: none;
    font-size: 12px;
    line-height: 16px;
    font-weight: 500;
    min-width: 0;
  }
  .ind {
    width: 64px;
    height: 32px;
    border-radius: 16px;
    display: flex;
    align-items: center;
    justify-content: center;
    position: relative;
    transition: background-color 150ms ease;
  }
  .item:hover .ind { background: color-mix(in srgb, var(--on-surface) 8%, transparent); }
  .on { color: var(--on-surface); }
  .on .ind,
  .on:hover .ind { background: var(--secondary-container); color: var(--on-secondary-container); }
  .lbl {
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
</style>
