# Leaf Website

Astro/Starlight website for Leaf. The documentation content is shared from the
repo-level `docs/` folder via `src/content/docs`.

## Documentation format

Every `.md` and `.mdx` file under the repo-level `docs/` folder is loaded by
Astro/Starlight, including backlog notes and UI review READMEs outside the sidebar.
Each file must begin with YAML frontmatter containing a `title` string:

```markdown
---
title: Logbook maintenance UI review
---

# Logbook maintenance UI review
```

A Markdown heading alone does not satisfy the content schema. Preserve any other
frontmatter fields when editing an existing page. Files outside `docs/` are not
part of this documentation collection.

Run `npm run build` from `website/` to validate all documentation and generate the
site. On Windows, first run the local preview script below to prepare the docs
junction. The `Website check` pull request workflow runs the same website build
for documentation, website, and website workflow changes. Pages deployment still
runs after merge to `main`.

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

### Repeatable Task: Start Local Website Server

From the repo root, this is the usual command when editing the website and
previewing from both the desktop and a phone on the same Wi-Fi:

```powershell
powershell -ExecutionPolicy Bypass -File .\website\scripts\start-local-preview.ps1 -Network -Background
```

Then open:

- desktop: `http://localhost:4321/`
- phone: the `Network:` URL printed by the script, usually similar to
  `http://10.0.0.14:4321/`

The server writes logs to:

- `website/astro-dev-preview.log`
- `website/astro-dev-preview.err.log`

If the page does not load immediately, give Astro a few seconds to finish
syncing content and then refresh.

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
