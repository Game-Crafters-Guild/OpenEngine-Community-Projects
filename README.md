# OpenEngine Community Projects

The community project catalog for the OpenEngine editor. The editor's
**New Project → Community** tab fetches [`manifest.json`](manifest.json) from
this repo and lists every entry as a cloneable project.

## How it works

- Each project lives in its own subfolder under [`projects/`](projects/).
- `manifest.json` at the repo root describes them. The editor downloads only
  the selected project's subtree (shallow sparse clone), so this repo can hold
  many projects without bloating installs.
- Projects hosted in *other* repositories work too: point `source.url` at any
  public `https://` git repo and omit `source.path` (whole repo) or set it to a
  subfolder.

## Adding a project

1. Add your project under `projects/<slug>/` — the folder is the project root
   (an `Assets/` folder with your scenes/content). Don't commit `.Cache/`,
   `AssetDatabase.assetdb`, or `.Editor/` — the editor regenerates them.
2. Add a `thumbnail.png` in your project folder (roughly 16:10; shown on the
   project card).
3. Append an entry to `manifest.json`:

```json
{
  "id": "my-project",              // lowercase slug: a-z 0-9 - _
  "name": "My Project",
  "description": "One or two lines shown on the card.",
  "author": "you",
  "engineVersion": "1.0",
  "thumbnail": "projects/my-project/thumbnail.png",
  "source": {
    "type": "git",
    "url": "https://github.com/Game-Crafters-Guild/OpenEngine-Community-Projects",
    "path": "projects/my-project"
  }
}
```

Optional entry fields: `stars` (number shown on the card), `accentColor`
(`"#RRGGBB"` card backdrop), `thumbnailFit: "contain"` for icon-style art,
`source.ref` for a branch/tag.

Manifest rules the editor enforces: `id` must be a plain slug and unique;
`source.url` must be `https://`; `source.path` must be a relative path with no
`..`. Entries that break the rules are skipped or shown as unsupported.
