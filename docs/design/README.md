# Design documents

`architecture.html` is the source; `RageV-Architecture.pdf` is generated from it.

It is a **knowledge-transfer document**: the goal is that an engineer with no
graphics background could follow the build order in part 2 and arrive at this
engine, rather than merely understand the one that exists. Part 2 is the spine;
everything after it is reference for a step in it.

Regenerate the PDF after editing the HTML:

```bash
"/c/Program Files/Google/Chrome/Application/chrome.exe" --headless=new --disable-gpu \
    --no-sandbox --user-data-dir=<a writable temp dir> --no-pdf-header-footer \
    --print-to-pdf=<a writable temp path> \
    "file:///C:/Users/ism19/Code/RageV/docs/design/architecture.html"
```

Two things that will waste time otherwise:

- Chrome refuses to write the PDF into the repository directly ("Access is
  denied"), and needs `--user-data-dir`. Print to a temp path and copy.
- **Chrome does not paint the root background when printing.** A page with no
  painted background looks white in most readers and renders **black** in one
  with a dark mode, because there is nothing to invert from. The stylesheet
  therefore paints an explicit white rectangle on every page via a fixed
  `body::before` in `@media print`. Verify it survived any CSS change by
  decompressing the PDF's content streams and looking for a white fill on
  every page — the regeneration output above reports that count.
- The HTML also pins `color-scheme: only light` and its own background, for
  the same reason on screen. Note that the Claude Code preview pane forces
  dark mode regardless; render with headless Chrome to see the true colours.
- Code blocks are light, not dark. A dark block on a light page inverts to
  light-on-light in a reader's dark mode and prints as a solid black
  rectangle.

Figures in `figures/` are downscaled JPEGs of real engine output, captured with
`--screenshot`. Never use desktop capture for these.
