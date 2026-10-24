<script>
  // How much the panel says on its USB console (GET / POST /api/loglevel, log_policy.cpp): the
  // shipping policy is WARN with a few tags held at INFO, because their lines have each settled a
  // fault. Opened up here, until the next restart: a switch that survived one would be left on.
  // The log itself is read on the console; the web has no journal to show (no log module).
  import { onMount } from 'svelte';
  import { t, K, fmt } from '../../stores/i18n.js';
  import { apiGet, apiPost } from '../../lib/api.js';
  import { toastError, toastSuccess } from '../../stores/toast.js';
  import Group from '../../m3/Group.svelte';
  import Row from '../../m3/Row.svelte';
  import Button from '../../m3/Button.svelte';
  import TextField from '../../m3/TextField.svelte';

  const LEVELS = ['none', 'error', 'warn', 'info', 'debug', 'verbose'];
  const NAMES = {
    none: K('log.none'), error: K('log.error'), warn: K('log.warn'),
    info: K('log.info'), debug: K('log.debug'), verbose: K('log.verbose'),
  };

  let v = null;
  let failed = '';
  let tag = '';
  let tagLevel = 'info';

  async function load() {
    try {
      v = await apiGet('/api/loglevel');
      failed = '';
    } catch (e) {
      failed = e.message;
    }
  }
  onMount(load);

  // nothing above what this build compiled in (CONFIG_LOG_MAXIMUM_LEVEL): the panel refuses it
  $: offered = v ? LEVELS.slice(0, Math.max(0, LEVELS.indexOf(v.ceiling)) + 1) : [];

  async function set(which, level) {
    try {
      await apiPost('/api/loglevel', { tag: which, level });
      toastSuccess(fmt($t['log.set'], { tag: which === '*' ? $t['log.all'] : which, level: $t[NAMES[level]] }));
    } catch (e) {
      toastError(e.message);
    }
    load();
  }
  const name = (l) => (NAMES[l] ? $t[NAMES[l]] : l);
</script>


{#if !v}
  <p class="msg">{failed || $t['app.loading']}</p>
{:else}
  <Group>
    <Row icon="article" title={$t['log.all']} subtitle={fmt($t['log.shipping'], { level: name(v.shipping_default) })}>
      <select class="pick" aria-label={$t['log.all']} value={v.global} on:change={(e) => set('*', e.target.value)}>
        {#each offered as l}<option value={l}>{name(l)}</option>{/each}
      </select>
    </Row>
  </Group>

  <Group caption={$t['log.kept']}>
    {#each v.kept as k (k.tag)}
      <Row title={k.tag}>
        <select class="pick" aria-label={k.tag} value={k.level} on:change={(e) => set(k.tag, e.target.value)}>
          {#each offered as l}<option value={l}>{name(l)}</option>{/each}
        </select>
      </Row>
    {/each}
  </Group>

  <Group caption={$t['log.other']}>
    <div class="other">
      <div class="tf"><TextField label={$t['log.tag']} bind:value={tag} maxlength={47} /></div>
      <select class="pick" aria-label={$t['log.level']} bind:value={tagLevel}>
        {#each offered as l}<option value={l}>{name(l)}</option>{/each}
      </select>
      <Button variant="tonal" disabled={!tag.trim() || /["\\]/.test(tag)} on:click={() => set(tag.trim(), tagLevel)}>{$t['log.apply']}</Button>
    </div>
  </Group>

  <div class="acts">
    <Button variant="outlined" icon="restart_alt" on:click={() => set('*', v.shipping_default)}>{$t['log.reset']}</Button>
  </div>
{/if}

<style>
  .msg { text-align: center; color: var(--on-surface-var); padding: 32px 16px; }
  .pick {
    height: 40px;
    max-width: 50%;
    border: 1px solid var(--outline-var);
    border-radius: 20px;
    padding: 0 12px;
    background: var(--secondary-container);
    color: var(--on-secondary-container);
    font: inherit;
    font-size: 15px;
    font-weight: 500;
  }
  .pick option { background: var(--surface-container); color: var(--on-surface); }
  .other { display: flex; flex-wrap: wrap; align-items: flex-end; gap: 10px; padding: 10px 12px 12px; }
  .tf { flex: 1 1 160px; min-width: 0; }
  .acts { padding: 16px 0 0; }
</style>
