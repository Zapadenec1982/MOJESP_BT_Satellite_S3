<script>
  // A Material Symbols Rounded glyph, the panel's icon set (icons.css, built by
  // tools/fonts/build_web_icons.py). Asked for by name, drawn by codepoint; a name that is not
  // in webui/src/lib/icon_names.json draws nothing and says so in the console.
  import codepoints from '../lib/icon_codepoints.js';

  export let name;
  export let size = 24;
  /** The filled face: the active nav item, a playing transport button. */
  export let fill = false;
  /** Set when the icon carries meaning of its own; otherwise it is hidden from screen readers. */
  export let label = '';

  $: cp = codepoints[name];
  $: if (cp === undefined && name) console.warn(`icon "${name}" is not in icon_names.json`);
  $: glyph = cp === undefined ? '' : String.fromCodePoint(cp);
</script>

<span
  class="ms"
  style="font-size:{size}px;font-variation-settings:'FILL' {fill ? 1 : 0}"
  role={label ? 'img' : null}
  aria-label={label || null}
  aria-hidden={label ? null : 'true'}
>{glyph}</span>

<style>
  .ms {
    font-family: 'P4 Symbols';
    font-weight: normal;
    font-style: normal;
    line-height: 1;
    display: inline-block;
    flex: none;
    width: 1em;
    height: 1em;
    text-align: center;
    white-space: nowrap;
    speak: never;
    user-select: none;
    -webkit-font-smoothing: antialiased;
  }
</style>
