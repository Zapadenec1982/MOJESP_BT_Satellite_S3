<script>
  // A text field: the label above, the box on surface_container, a 2 px primary ring while it is
  // being typed in — the panel's field (ui_m3_widgets.cpp:601-632) on a page.
  export let label;
  export let value = '';
  export let type = 'text';
  export let placeholder = '';
  export let autocomplete = 'off';
  export let id = `tf-${Math.random().toString(36).slice(2, 8)}`;
  export let disabled = false;
  export let maxlength = null;
  /** The phone's keyboard: 'decimal' for a measured value. */
  export let inputmode = null;

  function onInput(e) {
    value = e.target.value;
  }
</script>

<label class="tf" for={id}>
  <span class="lbl">{label}</span>
  <input
    {id}
    {type}
    {value}
    {placeholder}
    {autocomplete}
    {disabled}
    maxlength={maxlength ?? null}
    inputmode={inputmode ?? null}
    on:input={onInput}
    on:input
    on:keydown
  />
</label>

<style>
  .tf { display: flex; flex-direction: column; gap: 6px; min-width: 0; }
  .lbl { font-size: 14px; line-height: 20px; color: var(--on-surface-var); }
  input {
    height: 48px;
    padding: 0 14px;
    border-radius: var(--r-xs);
    border: 1px solid var(--outline);
    background: var(--surface-container);
    color: var(--on-surface);
    font-size: 16px;
    width: 100%;
  }
  input:focus {
    outline: none;
    border: 2px solid var(--primary);
    padding: 0 13px;
  }
  input:disabled { opacity: 0.38; }
</style>
