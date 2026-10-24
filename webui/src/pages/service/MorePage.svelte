<script>
  // Other settings: the long tail (lib/longtail.js) — what the generated pages carry that no page
  // of this interface shows, drawn as the generated cards, by the module page it came from. The
  // read-only values among them are polled with the rest of Service's.
  import { t } from '../../stores/i18n.js';
  import { pages, stateMeta } from '../../stores/ui.js';
  import { state } from '../../stores/state.js';
  import { live } from '../../lib/live.js';
  import { longTail } from '../../lib/longtail.js';
  import DynamicPage from '../DynamicPage.svelte';

  $: tail = longTail($pages);
  // a setting from the WebSocket (a poll could be older than the change just made), a reading
  // from the poll (it reaches no WebSocket delta)
  $: keys = tail.flatMap((p) => p.cards.flatMap((c) => c.widgets.map((w) => w.key)));
  $: values = Object.fromEntries(keys.map((k) => [k,
    $stateMeta[k]?.access === 'read' && k in $live ? $live[k] : $state[k] ?? $live[k]]));
</script>

{#each tail as p (p.id)}
  <h2 class="cap">{p.title}</h2>
  <DynamicPage source={p} {values} />
{:else}
  <p class="hint">{$t['svc.more_none']}</p>
{/each}

<style>
  .hint {
    padding: 4px 4px 0;
    font-size: 14px;
    line-height: 20px;
    color: var(--on-surface-var);
  }
  .cap {
    font-size: 12px;
    line-height: 16px;
    font-weight: 500;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--outline);
    padding: 20px 4px 8px;
    max-width: 640px;
    margin: 0 auto;
  }
</style>
