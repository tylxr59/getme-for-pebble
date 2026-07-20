# getme for Pebble

getme for Pebble is a Pebble watch app for [getme](https://github.com/tylxr59/getme), a dead-simple self-hosted grocery list.

## Install

Install getme for Pebble from the [Pebble Appstore](https://apps.repebble.com/6f7de6d295fa464aa9bc7ab8), or download a tagged PBW from [GitHub Releases](https://github.com/tylxr59/getme-for-pebble/releases).

<img src="demo_animation.gif" alt="getme for Pebble demo" width="144" height="168">

This is a new Pebble app based on a fork of [Checklist](https://github.com/freakified/PebbleChecklist). It keeps the fast, button-first checklist experience from Checklist, but changes the storage model so your self-hosted getme server is the source of truth.

## Features

- Syncs your getme list to a Pebble watch
- Shows the last synced list when the phone or server is unavailable
- Adds new grocery items with Pebble dictation
- Toggles items checked or unchecked from the watch
- Clears checked items from getme
- Rejects incomplete or stale syncs without replacing the last good list
- Uses a Clay settings page for the getme server URL
- Builds as its own app with a separate UUID from Checklist, so both apps can coexist

## How It Works

getme for Pebble talks to getme through PebbleKitJS on the paired phone. The watch sends add, toggle, clear, and refresh requests to the phone; the phone calls the getme JSON API; then the watch receives a fresh copy of the server list.

The watch keeps a local copy only as a last-known display state. getme remains the canonical list.

## Requirements

- Pebble Time 2 / `emery`
- A self-hosted [getme](https://github.com/tylxr59/getme) server reachable from the paired phone
- Pebble SDK / Pebble Tool, only when building locally

## Related Repos

- getme web app/API: <https://github.com/tylxr59/getme>
- getme for Pebble watch app: <https://github.com/tylxr59/getme-for-pebble>
- Original Checklist app: <https://github.com/freakified/PebbleChecklist>

## Setup

1. Install getme for Pebble from the Pebble app store, or build it locally.
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

## Controls

- **UP/DOWN**: Move through list items and actions.
- **SELECT**: Add an item, toggle the selected item, or clear completed items.
- **BACK**: Close a message, return to the list, or exit the app.

## Troubleshooting

- **Set GetMe URL**: Open companion settings and enter the complete getme URL.
- **Phone timeout** or **Message dropped**: Confirm the phone is connected, then reopen the app to retry. The last good list remains available.
- **List has more than 52 items**: Reduce the getme list to 52 items or fewer, then reopen the app.
- **Invalid server item** or **Invalid sync data**: Update getme and verify its JSON API returns positive item IDs and non-empty names.
- If the app shows stale items, reopen it with the phone connected to trigger another sync.
- Include the full getme path in settings, including any subdirectory used by the browser version.

## Development

Install dependencies, then build with the Pebble SDK:

```bash
npm ci
npm run build
```

The compiled package is written to `build/getme-for-pebble.pbw`.

Install it in the Time 2 emulator with:

```bash
npm run install:emery
```

## Releases

Pushing a version tag such as `v1.1.0` runs the GitHub Actions release workflow. The workflow verifies that the tag matches `package.json`, builds the PBW, uploads it as a workflow artifact, and attaches it to the matching GitHub Release. The release body is taken from the matching section of `CHANGELOG.md`.

To repair a missing or outdated PBW for an existing tag, open **Actions → Release PBW → Run workflow** and enter that tag. Manual runs check out the exact tag before rebuilding and replacing the release asset. Release current code with a new version and tag instead of reusing an older tag.

## Project Layout

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

## License

getme for Pebble is licensed under the [MIT License](LICENSE). It is based on [Checklist](https://github.com/freakified/PebbleChecklist); see the repository history and license for attribution.

## Notes

AI assistance was used during development for code review, UI polish, documentation drafting, and implementation support. The app behavior, configuration choices, and release decisions were reviewed by the project maintainer.
