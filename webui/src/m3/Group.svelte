<script>
  // A titled group of rows on surface_low, radius 24 — the panel's settings card
  // (screen_set.cpp:41-55). The caption sits above the card, in label size; `caps` sets it in
  // spaced capitals, as the panel's service page heads its groups (screen_svc.cpp).
  export let caption = '';
  export let caps = false;
</script>

<section class="grp">
  {#if caption}<h2 class="cap" class:caps>{caption}</h2>{/if}
  <div class="card"><slot /></div>
</section>

<style>
  .grp { display: flex; flex-direction: column; min-width: 0; }
  .cap {
    font-size: 14px;
    line-height: 20px;
    font-weight: 500;
    color: var(--on-surface-var);
    padding: 16px 16px 8px;
  }
  .caps {
    font-size: 12px;
    line-height: 16px;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--outline);
    padding: 20px 16px 8px;
  }
  .card {
    background: var(--surface-low);
    border-radius: var(--r-l);
    padding: 4px;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
</style>
