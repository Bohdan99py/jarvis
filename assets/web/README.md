# assets/web — vendored browser assets

## mermaid.min.js

Mermaid **11.16.1**, the self-contained IIFE bundle from
`https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js`.
Licence: MIT (© 2014–2024 Knut Sveidqvist) — see
<https://github.com/mermaid-js/mermaid/blob/develop/LICENSE>.

It is embedded into `jarvis.exe` through `assets/mermaid.qrc` and loaded by
`VisualInsightsWidget` as `qrc:/web/mermaid.min.js`. Vendoring it is what
makes the diagram panel work **without an internet connection** — the panel
used to `import` mermaid from a CDN, so offline it drew nothing at all.

The bundle is checked for having no dynamic `import()` calls, so a single
`<script src>` is enough; do not swap it for the `.esm.min.mjs` build, which
splits itself across lazily-imported chunk files.

### Updating

```bash
curl -sSL -o assets/web/mermaid.min.js https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js
```

Then confirm the tail still ends with `globalThis["mermaid"] = ...` (the
global the page relies on) and that `grep -c 'import('` returns 0.
