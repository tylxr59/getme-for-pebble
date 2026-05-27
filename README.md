# getme for Pebble

<img src="demo_animation.gif" width="144" height="168">

getme for Pebble is a Pebble watch app for [getme](https://github.com/tylxr59/getme), a dead-simple self-hosted grocery list.

This is a new Pebble app based on a fork of [Checklist](https://github.com/freakified/PebbleChecklist). It keeps the fast, button-first checklist experience from Checklist, but changes the storage model so your self-hosted getme server is the source of truth.

## Features

- Syncs your getme list to a Pebble watch
- Shows the last synced list when the phone or server is unavailable
- Adds new grocery items with Pebble dictation
- Toggles items checked or unchecked from the watch
- Clears checked items from getme
- Uses a Clay settings page for the getme server URL
- Builds as its own app with a separate UUID from Checklist, so both apps can coexist

## How It Works

getme for Pebble talks to getme through PebbleKitJS on the paired phone. The watch sends add, toggle, clear, and refresh requests to the phone; the phone calls the getme JSON API; then the watch receives a fresh copy of the server list.

The watch keeps a local copy only as a last-known display state. getme remains the canonical list.

## Related Repos

- getme web app/API: <https://github.com/tylxr59/getme>
- getme for Pebble watch app: <https://github.com/tylxr59/getme-for-pebble>
- Original Checklist app: <https://github.com/freakified/PebbleChecklist>

## Setup

1. Install or build getme for Pebble on your watch.
2. Open the app settings in the Pebble/Rebble companion app.
3. Set the full URL to your getme install.

Example:

```text
https://example.com/getme/
```

The URL should point to the same page or directory you use in a browser.

## Controls

- Select the plus row to add an item by dictation.
- Select an item to toggle it checked or unchecked.
- Select "Clear completed" to remove checked items from getme.

## Build

Install dependencies, then build with the Pebble SDK:

```bash
npm install
pebble build
```

## Notes

AI assistance was used during development for code review, UI polish, documentation drafting, and implementation support. The app behavior, configuration choices, and release decisions were reviewed by the project maintainer.