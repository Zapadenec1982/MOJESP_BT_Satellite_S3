<script>
  // One reading of the Service page: the name, and the value in its verdict's colour — accent to
  // watch, error for a result — as the panel paints a detail row (screen_svc.cpp). A fault is
  // also semibold: amber and red are close enough in hue to need a second cue.
  import { OK, WARN, BAD } from '../../lib/service.js';

  export let label;
  export let value;
  export let grade = OK;
</script>

<div class="rd">
  <span class="l">{label}</span>
  <span class="v" class:warn={grade === WARN} class:bad={grade === BAD}>{value}</span>
</div>

<style>
  .rd {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    min-height: 44px;
    padding: 6px 14px;
    border-top: 1px solid var(--surface-container);
  }
  .rd:first-child { border-top: 0; }
  .l { font-size: 15px; line-height: 20px; color: var(--on-surface-var); min-width: 0; }
  .v {
    font-size: 15px;
    line-height: 20px;
    color: var(--on-surface);
    font-variant-numeric: tabular-nums;
    text-align: right;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .warn { color: var(--primary); }
  .bad { color: var(--error); font-weight: 600; }
</style>
