<script>
  // Two languages. The panel's, ui.lang: its screen
  // speaks it, and so does every browser that has not picked one of its own. And this browser's:
  // "as the panel", or a language that stays here and changes nothing on the panel.
  import { onDestroy } from 'svelte';
  import { t, pick, pickLanguage, panelLanguage, LANGS, LANG_NAMES } from '../stores/i18n.js';
  import { createSettingSender } from '../lib/settings.js';
  import Group from '../m3/Group.svelte';
  import Row from '../m3/Row.svelte';
  import Icon from '../m3/Icon.svelte';

  const panelLang = createSettingSender('ui.lang', { debounceMs: 0 });
  onDestroy(() => panelLang.cleanup());
</script>

<Group caption={$t['lang.panel']}>
  {#each LANGS as lang, i (lang)}
    <Row button selected={lang === $panelLanguage} title={LANG_NAMES[lang]} on:click={() => panelLang.send(i)}>
      {#if lang === $panelLanguage}<Icon name="check" />{/if}
    </Row>
  {/each}
</Group>

<Group caption={$t['lang.browser']}>
  <Row button selected={!$pick} title={$t['lang.as_panel']} subtitle={LANG_NAMES[$panelLanguage] || ''}
       on:click={() => pickLanguage(null)}>
    {#if !$pick}<Icon name="check" />{/if}
  </Row>
  {#each LANGS as lang (lang)}
    <Row button selected={lang === $pick} title={LANG_NAMES[lang]} on:click={() => pickLanguage(lang)}>
      {#if lang === $pick}<Icon name="check" />{/if}
    </Row>
  {/each}
</Group>

<style>
</style>
