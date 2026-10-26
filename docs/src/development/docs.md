# How these docs are built

This site is an [mdBook](https://rust-lang.github.io/mdBook/) in `docs/`. Its pages hold little text of their own: they include sections of the repository's `README.md` and `docs/RESEARCH.md` (and the simulator's `red4ext/tests/sim/README.md`), marked in those files with comments:

```text
<!-- ANCHOR: navigation -->
...
<!-- ANCHOR_END: navigation -->
```

So the README and the research notes stay the one place to write things; the book picks the changes up on its next build. GitHub doesn't show the comments.

- **A new research section** (a new version): add `## Title (plugin 0.xx)` to `RESEARCH.md` with an anchor round its body (`<!-- ANCHOR: v0xx -->` after the heading, `<!-- ANCHOR_END: v0xx -->` before the next heading), a page `docs/src/research/v0xx.md` that includes it, and a line in `docs/src/SUMMARY.md` and `research/versions.md`.
- **A new README section:** wrap it in an anchor and include it from a page, or extend an existing anchor.
- **Images** live in `docs/src/images/` (the README links to them there too).
- **Locally:** `mdbook serve docs --open` (or `mdbook build docs`, output in `docs/book/`).
- **Deployment:** `.github/workflows/docs.yml` builds the book on every push to `main` that touches the docs, the README or the simulator README, and publishes it to GitHub Pages. In the repository's settings, *Pages > Build and deployment > Source* must be *GitHub Actions*.
