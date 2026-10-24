<script>
  // The radio's buffer line (ui_m3_widgets.cpp:860-940): the filled part a sine in primary, moving
  // while the stream plays and flat while it does not; the rest a flat surface_highest line that
  // ends in a primary dot.
  export let fraction = 0;
  export let moving = false;
  export let label = '';

  const clip = `wave-${Math.random().toString(36).slice(2, 8)}`;
  const W = 400;
  const H = 16;
  const MID = H / 2;

  // one wave twice as long as the line, slid by a period: the motion is a transform, not a redraw
  let wave = `M0 ${MID}`;
  for (let x = 4; x <= W * 2; x += 4) wave += ` L${x} ${(MID + 2.6 * Math.sin(x / 7)).toFixed(2)}`;
  const flat = `M0 ${MID} L${W * 2} ${MID}`;

  $: f = Math.min(1, Math.max(0, fraction));
  $: cut = Math.max(6, f * (W - 14));
</script>

<svg class="wave" viewBox="0 0 {W} {H}" preserveAspectRatio="none" role={label ? 'img' : null} aria-label={label || null} aria-hidden={label ? null : 'true'}>
  <defs>
    <clipPath id={clip}><rect x="0" y="0" width={cut} height={H} /></clipPath>
  </defs>
  <g clip-path="url(#{clip})">
    <path class="played" class:moving d={moving ? wave : flat} vector-effect="non-scaling-stroke" />
  </g>
  <line class="rest" x1={cut + 8} y1={MID} x2={W - 8} y2={MID} vector-effect="non-scaling-stroke" />
  <circle class="dot" cx={W - 3} cy={MID} r="2.5" />
</svg>

<style>
  .wave { width: 100%; height: 16px; display: block; overflow: visible; }
  .played {
    fill: none;
    stroke: var(--primary);
    stroke-width: 3;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .moving { animation: slide 1.6s linear infinite; }
  @keyframes slide {
    from { transform: translateX(0); }
    to { transform: translateX(-43.98px); } /* one period: 2π·7 */
  }
  .rest { stroke: var(--surface-highest); stroke-width: 3; stroke-linecap: round; }
  .dot { fill: var(--primary); }
</style>
