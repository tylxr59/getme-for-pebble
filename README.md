<p align="center">
  <img src="logo-144.png" alt="getme for Pebble logo" width="112" height="112">
</p>

<h1 align="center">getme for Pebble</h1>

<p align="center">
  Your self-hosted getme grocery list, on your Pebble Time 2.
</p>

<p align="center">
  <a href="https://github.com/tylxr59/getme-for-pebble/actions/workflows/ci.yml"><img alt="Release status" src="https://github.com/tylxr59/getme-for-pebble/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/tylxr59/getme-for-pebble/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/tylxr59/getme-for-pebble?display_name=tag&sort=semver"></a>
  <a href="https://apps.repebble.com/6f7de6d295fa464aa9bc7ab8"><img alt="Pebble Appstore" src="https://img.shields.io/badge/Pebble%20Appstore-getme-ff4700"></a>
  <a href="#-requirements"><img alt="Pebble Time 2" src="https://img.shields.io/badge/Pebble-Time%202-ff4700"></a>
  <a href="LICENSE"><img alt="MIT license" src="https://img.shields.io/github/license/tylxr59/getme-for-pebble"></a>
</p>

<p align="center">
  <a href="#-features">Features</a> ·
  <a href="#-quick-start">Quick start</a> ·
  <a href="#-controls">Controls</a> ·
  <a href="#-troubleshooting">Troubleshooting</a>
</p>

---

## ⌚ getme for Pebble

getme for Pebble is a Pebble Time 2 app for [getme](https://github.com/tylxr59/getme), a dead-simple self-hosted grocery list.

<p align="center">
  <img src="demo_animation.gif" alt="getme for Pebble demo" width="216" height="252">
</p>

This is a new Pebble app based on a fork of [Checklist](https://github.com/freakified/PebbleChecklist). It keeps the fast, button-first checklist experience from Checklist, but changes the storage model so your self-hosted getme server is the source of truth.

> [!TIP]
> Don't have a getme server yet? [Set one up](https://github.com/tylxr59/getme#-quick-start) first. It's one PHP app and a SQLite file.

## 🎯 Features

| Feature                        | What it does                                                        |
| ------------------------------ | ------------------------------------------------------------------- |
| 🔄 **Sync**                    | Your getme list, on your watch.                                     |
| 📴 **Works offline**           | Shows the last synced list when the phone or server is unavailable. |
| 🎙️ **Dictation**               | Add new items by voice with Pebble dictation.                       |
| ✅ **Instant check-off**       | Items toggle immediately and save to getme in the background.       |
| 🧹 **Clear checked**           | Remove checked items from getme in one step.                        |
| 🛡️ **Safe syncing**            | Incomplete or stale syncs never replace the last good list.         |
| ⚙️ **Simple setup**            | A Clay settings page for your getme server URL.                     |
| 🤝 **Coexists with Checklist** | Its own UUID, so both apps can be installed.                        |

## 🔍 How it works

getme for Pebble talks to getme through PebbleKitJS on the paired phone. The watch sends add, toggle, clear, and refresh requests to the phone; the phone calls the getme JSON API; then the watch receives a fresh copy of the server list.

Checking items off is shown on the watch immediately and saved in the background, so you can keep checking items while earlier changes save. Rows stay in place until the next refresh, even though getme moves checked items to the bottom. Temporary connection failures keep edits pending for retry after a refresh or reopen. Rejected or conflicting edits show an error and reconcile with the server.

The watch keeps a cached list and a persistent outbox of pending edits. getme remains the canonical list.

## 📋 Requirements

- Pebble Time 2 / `emery`
- An upgraded self-hosted [getme](https://github.com/tylxr59/getme) server with the versioned sync API, reachable from the paired phone
- Pebble SDK / Pebble Tool, only when building locally

## 🚀 Quick start

Install getme for Pebble from the [Pebble Appstore](https://apps.repebble.com/6f7de6d295fa464aa9bc7ab8), or download a tagged PBW from [GitHub Releases](https://github.com/tylxr59/getme-for-pebble/releases).

1. Upgrade your getme server first: upload `index.php`, `server/`, and `web/` together, preserving the existing database. Then install getme for Pebble from the Pebble Appstore, or build it locally.
2. Open the app settings in the Pebble/Rebble companion app.
3. Set the full URL to your getme install.

Example:

```text
https://example.com/getme/
```

The URL should point to the same page or directory you use in a browser.

getme is intentionally unauthenticated: anyone who can reach this URL can read
and change the list. Use HTTPS, avoid sharing the URL, and follow getme's
database-protection guidance when exposing it outside a trusted network.

### Upgrading to 2.0.0

Requires the matching getme server migration. Deploy the server first: upload
`index.php`, `server/`, and `web/` together, keeping the existing SQLite database.
Older watch releases continue to work through the server's compatibility API.

The watch now persists pending actions and retries the same operation after a
connection failure or app restart. A completed snapshot confirms a server save;
Bluetooth delivery alone does not. Refresh or reopen to retry a pending action.
A conflicting edit is reported and the list reconciles with the server; it is
not silently reapplied. Changing servers discards pending edits with an error.

Lists longer than 52 items show the first 52 server-ordered items and a total count.
Clear checked deletes only the specific checked items visible on the watch.

## 🎮 Controls

- **UP/DOWN**: Move through list items and actions.
- **SELECT**: Add an item, toggle the selected item, or clear completed items.
- **BACK**: Close a message, return to the list, or exit the app.

## 🩺 Troubleshooting

- **Set GetMe URL**: Open companion settings and enter the complete getme URL.
- **Bad server response**: The server returned a successful HTTP response that was not valid JSON. Use the final HTTPS URL with a trailing slash (for example, `https://example.com/getme/`). HTTP or directory redirects can change the API POST into a GET and return the web page instead. Older app versions may keep using a previously saved URL after you change settings; update the app and save the correct URL again.
- **Phone timeout** or **Message dropped**: Confirm the phone is connected, then reopen the app to retry. The last good list remains available.
- **Showing 52 of N**: The watch displays the first 52 server-ordered items. Other items remain on the server. Clear checked affects only the visible checked items.
- **Not saved; refresh to retry**: Reopen or refresh with the phone connected to retry pending edits.
- **List changed**: The server rejected a stale edit. Let the list refresh, then retry your intended change.
- **Invalid server item** or **Invalid sync data**: Update getme and verify its JSON API returns positive item IDs and non-empty names.
- If the app shows stale items, reopen it with the phone connected to trigger another sync.
- Include the full getme path in settings, including any subdirectory used by the browser version.

## 👋 Development

Install dependencies, then build with the Pebble SDK:

```bash
npm ci
npm test
npm run build
```

The compiled package is written to `build/getme-for-pebble.pbw`.

Install it in the Time 2 emulator with:

```bash
npm run install:emery
```

Modules: `src/pkjs/{settings,http-client,sync,watch-transport}.js` handle phone work;
`src/c/{sync_state,pending_store,watch_protocol}.*` isolate durable state, journal,
and request encoding from the sync lifecycle in `messaging.c`. The existing list
model and windows remain responsible for storage/rendering and interaction.

Run `npm test` for restart, conflict, URL settings, UTF-8, and native journal fault
tests, then `pebble build` for emery. Physical disconnect/reconnect, dictation,
settings changes, and display behavior still need emulator/device testing.

### Project layout

```text
src/c/                 Pebble C app, windows, and list behavior
src/pkjs/              PebbleKitJS bridge and Clay settings
resources/images/      Menu and interface graphics
CHANGELOG.md            Versioned user-facing release notes
tools/                  Release-note extraction helper
package.json           Pebble metadata, scripts, and message keys
wscript                Pebble SDK build script
pebble-appstore.md     Version-controlled Pebble Appstore listing copy
```

## 📦 Releases

Pushing a version tag such as `v1.1.0` runs the GitHub Actions release workflow. The workflow verifies that the tag matches `package.json`, builds the PBW, uploads it as a workflow artifact, and attaches it to the matching GitHub Release. The release notes start with the matching section of `CHANGELOG.md`, followed by GitHub's generated notes (merged PRs, new contributors, and a compare link).

To repair a missing or outdated PBW for an existing tag, open **Actions → Release PBW → Run workflow** and enter that tag. Manual runs check out the exact tag before rebuilding and replacing the release asset. Release current code with a new version and tag instead of reusing an older tag.

## 🤝 Acknowledgements

Based on [Checklist](https://github.com/freakified/PebbleChecklist) by freakified.

AI assistance was used during development for code review, UI polish, documentation drafting, and implementation support. The app behavior, configuration choices, and release decisions were reviewed by the project maintainer.

## 📃 License

getme for Pebble is licensed under the [MIT License](LICENSE). It is based on [Checklist](https://github.com/freakified/PebbleChecklist); see the repository history and license for attribution.
