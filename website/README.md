# Leaf Website

Astro/Starlight website for Leaf. The documentation content is shared from the
repo-level `docs/` folder via `src/content/docs`.

## Local Preview

From `website/`, run:

```powershell
.\scripts\start-local-preview.ps1
```

The script:

- ensures `src/content/docs` points at the repo-level `docs/` folder
- uses the bundled local Node runtime under `.local-node/` when present
- starts Astro on `http://localhost:4321/`

To preview from another device on the same Wi-Fi, such as a phone:

```powershell
.\scripts\start-local-preview.ps1 -Network
```

Then open the LAN URL printed by the script, for example
`http://10.0.0.14:4321/`.

## Build

```powershell
.\.local-node\node-v24.14.0-win-x64\node.exe .\node_modules\astro\astro.js build
```

Or, if system Node/npm is available:

```powershell
npm run build
```

## Notes

On Windows, `src/content/docs` may appear in `git status` after local preview
setup because the script replaces a checkout placeholder with a local junction
to `../docs`. Do not stage that local-only change.
