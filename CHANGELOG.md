# Changelog

## 1.1.0

- Fixed list persistence at the 52-item limit and rejected damaged saved data safely.
- Improved phone synchronization with ordered, timeout-protected transfers that preserve the last good list when a refresh fails.
- Changed getme requests to use the documented JSON API and added clearer errors for oversized or invalid lists.
- Reduced the app's watch resources by removing unused inherited checklist animation assets.
- Added clearer security guidance for self-hosted getme URLs.

## 1.0

- Added the initial Pebble Time 2 client for syncing, adding, checking, and clearing items on a self-hosted getme list.
