<script>
  // The hardware this build is wired to (GET /api/bindings, boards/<board>/bindings.json): each
  // role, the driver that serves it and the resource it sits on, by the module that owns it. To
  // look at only — a wrong binding leaves the panel without a screen or sound, and the board's
  // file is changed in the repository, not from a phone.
  import { onMount } from 'svelte';
  import { t } from '../../stores/i18n.js';
  import { apiGet } from '../../lib/api.js';
  import Group from '../../m3/Group.svelte';
  import Row from '../../m3/Row.svelte';

  let list = null;
  let failed = '';
  onMount(async () => {
    try {
      list = (await apiGet('/api/bindings')).bindings || [];
    } catch (e) {
      failed = e.message;
    }
  });

  $: byModule = (list || []).reduce((m, b) => {
    const k = b.module || '—';
    (m[k] = m[k] || []).push(b);
    return m;
  }, {});
</script>

{#if !list}
  <p class="msg">{failed || $t['app.loading']}</p>
{:else}
  {#each Object.entries(byModule) as [mod, items] (mod)}
    <Group caption={mod}>
      {#each items as b (b.role)}
        <Row title={b.role} subtitle={[b.driver, b.hardware].filter(Boolean).join(' · ')} />
      {/each}
    </Group>
  {/each}
{/if}

<style>
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px; }
</style>
