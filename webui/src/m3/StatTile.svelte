<script>
  // A big number with its caption, as on the panel's service page (screen_svc.cpp): the
  // number turns primary on "watch" and error on "fault", the panel's own thresholds deciding.
  export let caption;
  export let value;
  export let unit = '';
  /** @type {'ok'|'warn'|'bad'} */
  export let status = 'ok';
</script>

<div class="stat">
  <span class="cap">{caption}</span>
  <span class="num {status}">{value}{#if unit}<span class="unit">{unit}</span>{/if}</span>
</div>

<style>
  .stat {
    background: var(--surface-container);
    border-radius: var(--r-l);
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }
  .cap {
    font-size: 12px;
    line-height: 16px;
    font-weight: 500;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--outline);
  }
  .num { font-size: 32px; line-height: 38px; font-weight: 600; }
  .num.warn { color: var(--primary); }
  .num.bad { color: var(--error); }
  .unit { font-size: 14px; font-weight: 400; color: var(--on-surface-var); margin-left: 4px; }
</style>
