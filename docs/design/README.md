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
- The HTML pins `color-scheme: only light` and its own background on purpose.
  Without that a viewer in dark mode inverts the page and leaves the body text
  unreadable — the PDF happens to survive because print media ignores the
  override, so the bug is invisible from the deliverable.

Figures in `figures/` are downscaled JPEGs of real engine output, captured with
`--screenshot`. Never use desktop capture for these.
