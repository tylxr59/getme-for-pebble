module.exports = function (Pebble, keys) {
var messageKeys = keys;
var MAX_CHECKLIST_ITEMS = 52;
var MAX_NAME_BYTES = 89;

// The watch's name buffer is measured in UTF-8 bytes, not JS characters.
function truncateItemName(name) {
  var bytes = 0;
  var end = 0;
  while (end < name.length) {
    var code = name.charCodeAt(end);
    var units = 1;
    var size = code < 0x80 ? 1 : code < 0x800 ? 2 : 3;
    if (code >= 0xD800 && code <= 0xDBFF && end + 1 < name.length &&
        name.charCodeAt(end + 1) >= 0xDC00 &&
        name.charCodeAt(end + 1) <= 0xDFFF) {
      units = 2;
      size = 4;
    }
    if (bytes + size > MAX_NAME_BYTES) break;
    bytes += size;
    end += units;
  }
  return name.substring(0, end);
}

var KEY_FETCH_REQUEST = messageKeys.KEY_FETCH_REQUEST;
var KEY_ADD_REQUEST = messageKeys.KEY_ADD_REQUEST;
var KEY_TOGGLE_REQUEST = messageKeys.KEY_TOGGLE_REQUEST;
var KEY_CLEAR_CHECKED_REQUEST = messageKeys.KEY_CLEAR_CHECKED_REQUEST;
var KEY_REQUEST_SEQ = messageKeys.KEY_REQUEST_SEQ;
var KEY_CONFIG_CHANGED = messageKeys.KEY_CONFIG_CHANGED;
var KEY_PHONE_READY = messageKeys.KEY_PHONE_READY;
var KEY_ITEM_ID = messageKeys.KEY_ITEM_ID;
var KEY_ITEM_NAME = messageKeys.KEY_ITEM_NAME;
var KEY_ITEM_CHECKED = messageKeys.KEY_ITEM_CHECKED;
var KEY_ITEM_INDEX = messageKeys.KEY_ITEM_INDEX;
var KEY_SYNC_BEGIN = messageKeys.KEY_SYNC_BEGIN;
var KEY_SYNC_ITEM = messageKeys.KEY_SYNC_ITEM;
var KEY_SYNC_DONE = messageKeys.KEY_SYNC_DONE;
var KEY_SYNC_ERROR = messageKeys.KEY_SYNC_ERROR;

function sendMessage(dict, callback) {
  Pebble.sendAppMessage(dict, function () {
    if (callback) callback(null);
  }, function () {
    console.log('Failed to send AppMessage: ' + JSON.stringify(dict));
    if (callback) callback(new Error('Phone transfer failed'));
  });
}

function notifyWatch(key, attemptsRemaining) {
  var dict = {};
  dict[key] = 2;
  sendMessage(dict, function (err) {
    if (err && attemptsRemaining > 1) {
      setTimeout(function () {
        notifyWatch(key, attemptsRemaining - 1);
      }, 500);
    }
  });
}

function sendError(seq, error, callback) {
  var message = error && error.message ? error.message :
    String(error || 'Sync failed');
  var dict = {};
  dict[KEY_REQUEST_SEQ] = seq;
  dict[KEY_SYNC_ERROR] = message.substring(0, 60);
  dict[messageKeys.KEY_RETRYABLE] = error && error.retryable ? 1 : 0;
  sendMessage(dict, callback);
}

function sendItems(seq, snapshot, callback) {
  var items = snapshot.items;
  var total = items.length;
  items = items.slice(0, MAX_CHECKLIST_ITEMS);

  var index = 0;
  var begin = {};
  begin[KEY_REQUEST_SEQ] = seq;
  begin[KEY_SYNC_BEGIN] = items.length;
  begin[messageKeys.KEY_PROTOCOL_VERSION] = 2;
  begin[messageKeys.KEY_TOTAL_ITEMS] = total;
  begin[messageKeys.KEY_REVISION] = snapshot.revision;

  sendMessage(begin, function (beginError) {
    if (beginError) return callback(beginError);

    function sendNext() {
      if (index >= items.length) {
        var done = {};
        done[KEY_REQUEST_SEQ] = seq;
        done[KEY_SYNC_DONE] = 1;
        sendMessage(done, callback);
        return;
      }

      var item = items[index];
      var itemId = parseInt(item.id, 10);
      var itemName = truncateItemName(String(item.name || ''));
      if (!(itemId > 0) || !itemName) {
        return callback(new Error('Invalid server item'));
      }

      var dict = {};
      dict[KEY_REQUEST_SEQ] = seq;
      dict[KEY_SYNC_ITEM] = 1;
      dict[KEY_ITEM_INDEX] = index;
      dict[KEY_ITEM_ID] = itemId;
      dict[KEY_ITEM_NAME] = itemName;
      dict[KEY_ITEM_CHECKED] = Number(item.checked) === 1 ? 1 : 0;
      index++;
      sendMessage(dict, function (itemError) {
        if (itemError) return callback(itemError);
        sendNext();
      });
    }

    sendNext();
  });
}


return { notify: notifyWatch, error: sendError, snapshot: sendItems };
};
