import svelte from 'rollup-plugin-svelte';
import resolve from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import terser from '@rollup/plugin-terser';
import css from 'rollup-plugin-css-only';
import json from '@rollup/plugin-json';
import { mkdirSync, writeFileSync } from 'fs';

const production = !process.env.ROLLUP_WATCH;

export default {
  input: 'src/main.js',
  // the entry's exports are nobody's API: let the code a chunk shares with it stay in bundle.js
  // rather than become a third file every visit has to fetch
  preserveEntrySignatures: false,
  // ES modules, so a page that is rarely opened (the radio catalog) is its own file, fetched the
  // first time it is needed: the bundle every visit loads stays small (PLAN_WEBUI §5, budget).
  // Stable names, no hashes: the device serves everything no-store, and data.bin depends on
  // dist's output through data/www/build.txt (scripts/deploy.js).
  output: {
    sourcemap: !production,
    format: 'es',
    dir: 'dist',
    entryFileNames: 'bundle.js',
    chunkFileNames: 'chunk-[name].js'
  },
  plugins: [
    svelte({
      compilerOptions: {
        dev: !production
      }
    }),
    // One bundle.css for every component, chunks included. css-only's own output follows a chunk's
    // static imports only, and dropped the styles of the pages that load on demand (StationsPage);
    // its `styles` map has them all. The global sheets main.js imports go first, in its order;
    // the components' scoped rules after them.
    css({
      output: (_, styles) => {
        const GLOBAL = ['m3_schemes.css', 'icons.css', 'base.css', 'legacy.css'];
        const rank = (id) => {
          const i = GLOBAL.findIndex((g) => id.endsWith('/styles/' + g));
          return i < 0 ? GLOBAL.length : i;
        };
        const ids = Object.keys(styles).sort((a, b) => rank(a) - rank(b));
        mkdirSync('dist', { recursive: true });
        writeFileSync('dist/bundle.css', ids.map((id) => styles[id]).join('\n') + '\n');
      }
    }),
    resolve({
      browser: true,
      dedupe: ['svelte']
    }),
    json({ preferConst: true }),
    commonjs(),
    production && terser()
  ],
  watch: {
    clearScreen: false
  }
};
