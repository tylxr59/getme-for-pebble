# Changelog

## Unreleased

- Made checking items off instant: changes save in the background, so you can keep checking items without waiting.
- Kept list rows in place while checking items off, so a background save no longer shifts the next item under the cursor.
- Restored an item's previous state and showed one error when a change cannot be saved.

## 1.2.0

- Fixed long item names being cut in the middle of accented characters or emoji.
- Removed trailing periods from dictated item names.
- Kept the list selection in range after a sync removes items.
- Fixed server URL changes being ignored when a previous URL was cached, including switching from HTTP to HTTPS.
- Fixed clearing an invalid server URL so it no longer restores the previous setting.

## 1.1.0

- Fixed list persistence at the 52-item limit and rejected damaged saved data safely.
- Improved phone synchronization with ordered, timeout-protected transfers that preserve the last good list when a refresh fails.
- Changed getme requests to use the documented JSON API and added clearer errors for oversized or invalid lists.
- Reduced the app's watch resources by removing unused inherited checklist animation assets.
- Added clearer security guidance for self-hosted getme URLs.

## 1.0

- Added the initial Pebble Time 2 client for syncing, adding, checking, and clearing items on a self-hosted getme list.
