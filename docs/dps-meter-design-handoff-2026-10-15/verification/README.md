# Preview checks

These checks load `../preview.html` from disk with browser networking blocked. They do not open or test Last Epoch, build native code, or change saves. The preview itself needs no installation; only this developer checker needs Node.js and Playwright.

From this `verification` directory:

```powershell
npm install
npm test
```

The checker uses installed Chrome/Edge on Windows when available, otherwise Playwright's installed Chromium. You can specify another Chromium executable through `CHROME_PATH`. If no browser is installed, install Playwright Chromium for development with `npx playwright install chromium`.

`PLAYWRIGHT_MODULE` may point to an existing Playwright module rather than installing another copy. No absolute machine-specific module path is stored in the checker.

The script writes `preview-results.json` and the three PNGs in `../screenshots`. Assertions cover 12 default layout cases (three designs at 1024/736/390/320 px viewport widths), seven damage types, no horizontal clipping or sibling collisions, 22 sample interaction checks, screenshot/atlas decoding, absence of requests to the internet, and absence of page errors. These are **fixture preview checks**, not renderer/backend/game tests. Rerunning the checks changes generated files and requires regenerating `MANIFEST.json` if the package is redistributed.
