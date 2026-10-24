<script>
  import { t } from '../../stores/i18n.js';
  import { TIMEZONES } from '../../lib/timezones.js';

  export let config;
  export let value;

  const timezones = TIMEZONES;
</script>

<div class="tz-widget">
  <div class="tz-label">{config.description || $t['tz.label']}</div>
  <select class="tz-select">
    {#each timezones as z}
      <option value={z.tz}>{z.label}</option>
    {/each}
  </select>
</div>

<style>
  .tz-widget { padding: 4px 0; }
  .tz-label { font-size: 14px; color: var(--fg-muted); margin-bottom: 8px; }
  .tz-select {
    width: 100%;
    padding: 10px 12px;
    font-size: 14px;
    min-height: 44px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--fg);
    cursor: pointer;
    appearance: auto;
  }
  .tz-select:focus { outline: none; border-color: var(--accent); }
</style>
