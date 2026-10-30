# Pebble Appstore Listing

Status: Published

Listing: [getme for Pebble on the Pebble Appstore](https://apps.repebble.com/6f7de6d295fa464aa9bc7ab8)

Keep this file synchronized with the listing copy so Pebble Appstore text changes remain visible in version history.

## Name

getme for Pebble

## Description

Keep your self-hosted getme grocery list on your wrist.

getme for Pebble brings a fast, button-first checklist experience to Pebble, backed by your own getme server. Sync your list from your phone, check items off while shopping, add new items with Pebble dictation, and clear completed items when you're done.

Features:

- Syncs with your self-hosted getme grocery list
- Add items from the watch using dictation
- Toggle items checked or unchecked
- Clear specific visible completed items from getme
- Keeps showing your last synced list if your phone or server is unavailable
- Preserves pending edits across restarts and retries without duplicate additions
- Reports conflicting edits instead of silently overwriting another device
- Shows the total count when a list exceeds the 52-item watch view
- Protects the last good list from incomplete or stale syncs
- Configure your getme URL from the Pebble/Rebble companion app

Version 2.0 requires the upgraded getme server with the versioned sync API. Upgrade your server first by uploading index.php, server/, and web/ together; your existing list data is preserved. Requires Pebble Time 2, a paired phone, and a configured server URL. The watch displays the first 52 items; additional items remain on the server. getme is intentionally unauthenticated, so use HTTPS and treat the configured URL as private. Check it out at https://github.com/tylxr59/getme
