// Build the player's own icon glyphs into a font that lv_font_conv can merge with the
// Material Symbols faces.
//
//   node build_glyphs.js <out.ttf>        (build_fonts.py runs this; there is no other caller)
//
// For the day Material Symbols lacks a mark a source card needs: the glyph is drawn in
// glyphs/<name>.svg on a 24-unit grid and mapped into a 1000-unit em with the ascender at
// 900 — x * 1000/24 and 900 - y * 1000/24 — so lv_font_conv renders it at the same --size
// and baseline as the rest of the set. The AirPlay mark this once built is in Material
// Symbols (`airplay`), which is why the table below is empty; the codepoints, when there
// are any, are private-use (U+F800..F8FF) and must match OWN_ICONS in build_fonts.py.

'use strict';

const fs = require('fs');
const path = require('path');
const svg2ttf = require('svg2ttf');
const svgpath = require('svgpath');

const GLYPHS = [
    // { name: 'example', cp: 0xF800 },
];

const K = 1000 / 24;

function outline(name) {
    const svg = fs.readFileSync(path.join(__dirname, 'glyphs', `${name}.svg`), 'utf8');
    const paths = [...svg.matchAll(/<path\s+d="([^"]+)"/g)].map((m) => m[1]);
    if (!paths.length) throw new Error(`glyphs/${name}.svg: no <path d="...">`);
    // SVG is y-down, a font y-up: the flip is part of the matrix, and svgpath carries the
    // arcs through it (their sweep reverses with the flip).
    return paths.map((d) => svgpath(d).matrix([K, 0, 0, -K, 0, 900]).round(2).toString()).join(' ');
}

const out = process.argv[2];
if (!out) {
    console.error('usage: node build_glyphs.js <out.ttf>');
    process.exit(2);
}
if (!GLYPHS.length) {
    console.error('build_glyphs.js: GLYPHS is empty — nothing to build (build_fonts.py only calls this when OWN_ICONS has entries)');
    process.exit(2);
}

const glyphs = GLYPHS.map((g) =>
    `<glyph glyph-name="${g.name}" unicode="&#x${g.cp.toString(16)};" horiz-adv-x="1000" d="${outline(g.name)}"/>`
).join('\n');

const font = `<?xml version="1.0" standalone="no"?>
<svg xmlns="http://www.w3.org/2000/svg"><defs>
<font id="player-glyphs" horiz-adv-x="1000">
<font-face font-family="player-glyphs" units-per-em="1000" ascent="900" descent="-100"/>
<missing-glyph horiz-adv-x="1000"/>
${glyphs}
</font></defs></svg>`;

fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
// ts pins the header timestamp, so the same glyphs make the same file.
fs.writeFileSync(out, Buffer.from(svg2ttf(font, { ts: 0 }).buffer));
