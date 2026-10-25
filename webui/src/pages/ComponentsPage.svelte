<script>
  // Every component of the web's design system on one page, in the current scheme — what the
  // panel's m3test screen is for the glass. Not in any menu: #/components.
  import { t } from '../stores/i18n.js';
  import Button from '../m3/Button.svelte';
  import IconButton from '../m3/IconButton.svelte';
  import Chip from '../m3/Chip.svelte';
  import Switch from '../m3/Switch.svelte';
  import Slider from '../m3/Slider.svelte';
  import Group from '../m3/Group.svelte';
  import Row from '../m3/Row.svelte';
  import Tabs from '../m3/Tabs.svelte';
  import Stepper from '../m3/Stepper.svelte';
  import StatTile from '../m3/StatTile.svelte';
  import TextField from '../m3/TextField.svelte';
  import Dialog from '../m3/Dialog.svelte';
  import { toastSuccess, toastError } from '../stores/toast.js';

  let sw = true;
  let vol = 42;
  let named = 25;
  let tab = 'a';
  let gain = 3;
  let text = '';
  let dialog = false;
  let chip = true;
</script>

<div class="demo">
  <Group caption="Button">
    <div class="line">
      <Button>Filled</Button>
      <Button variant="tonal" icon="add">Tonal</Button>
      <Button variant="outlined">Outlined</Button>
      <Button variant="text">Text</Button>
      <Button variant="outlined" danger icon="restart_alt">Danger</Button>
      <Button disabled>Disabled</Button>
    </div>
  </Group>

  <Group caption="IconButton · Chip · Switch">
    <div class="line">
      <IconButton icon="skip_previous" label="prev" variant="tonal" />
      <IconButton icon="stop" label="stop" variant="filled" />
      <IconButton icon="skip_next" label="next" variant="tonal" />
      <IconButton icon="repeat" label="repeat" selected />
      <IconButton icon="queue_music" label="list" variant="outlined" />
      <Chip icon="radio" selected href="#/source">Радіо</Chip>
      <Chip>44.1 kHz 128 kbps</Chip>
      <Chip button selected={chip} on:click={() => (chip = !chip)}>PEQ</Chip>
      <Switch bind:checked={sw} label="switch" />
      <Switch checked={false} label="off" />
    </div>
  </Group>

  <Group caption="Slider">
    <div class="pad">
      <Slider label="volume" icon="volume_up" bind:value={vol} />
      <Slider label="name" name="Name" bind:value={named} min={5} format={(v) => `${v} %`} />
    </div>
  </Group>

  <Group caption="Row">
    <Row icon="wifi" title="Wi-Fi" subtitle="update · 10.133.16.120" href="#/components" />
    <Row icon="airplay" title="AirPlay" subtitle="P4 Player"><Switch bind:checked={sw} label="AirPlay" /></Row>
    <Row list icon="radio" title="SomaFM Groove Salad" subtitle="ice1.somafm.com · 1" selected />
  </Group>

  <Group caption="Tabs · Stepper · StatTile · TextField">
    <div class="pad col">
      <Tabs items={[{ id: 'a', label: 'Улюблені' }, { id: 'b', label: 'Топ' }, { id: 'c', label: 'Пошук' }]} bind:value={tab} />
      <Stepper caption="Gain" text={`${gain > 0 ? '+' : ''}${gain.toFixed(1)} dB`} decLabel="−" incLabel="+"
               on:dec={() => (gain -= 0.5)} on:inc={() => (gain += 0.5)} />
      <div class="stats">
        <StatTile caption="CPU" value="32" unit="%" />
        <StatTile caption="Loop" value="49" unit="ms" status="bad" />
        <StatTile caption="RAM" value="121" unit="KB" status="warn" />
      </div>
      <TextField label={$t['auth.user']} bind:value={text} />
    </div>
  </Group>

  <Group caption="Dialog · Snackbar">
    <div class="line">
      <Button variant="tonal" on:click={() => (dialog = true)}>Dialog</Button>
      <Button variant="tonal" on:click={() => toastSuccess($t['alert.saved'])}>Snackbar</Button>
      <Button variant="tonal" on:click={() => toastError($t['alert.error'])}>Error</Button>
    </div>
  </Group>
</div>

{#if dialog}
  <Dialog title={$t['alert.confirm_ota']} on:close={() => (dialog = false)}>
    {$t['bind.saved_msg']}
    <svelte:fragment slot="actions">
      <Button variant="text" on:click={() => (dialog = false)}>{$t['bind.later']}</Button>
      <Button on:click={() => (dialog = false)}>{$t['bind.restart']}</Button>
    </svelte:fragment>
  </Dialog>
{/if}

<style>
  .demo { display: flex; flex-direction: column; gap: 4px; }
  .line { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; padding: 12px; }
  .pad { padding: 8px 12px; }
  .col { display: flex; flex-direction: column; gap: 12px; }
  .stats { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }
</style>
