var Clay = require('@rebble/clay');
var keys = require('message_keys');
var clay = new Clay(require('./config'), null, { autoHandleEvents: false });
var settings = require('./settings')(localStorage, keys);
var watch = require('./watch-transport')(Pebble, keys);
var sync = require('./sync')(localStorage, settings.url,
  require('./http-client')(XMLHttpRequest), watch);

Pebble.addEventListener('ready', function () {
  var url = settings.url();
  var previous = localStorage.getItem('last-server');
  localStorage.setItem('last-server', url);
  watch.notify(previous !== null && previous !== url ? keys.KEY_CONFIG_CHANGED : keys.KEY_PHONE_READY, 3);
});
Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});
Pebble.addEventListener('webviewclosed', function (event) {
  if (!event || !event.response) return;
  var previous = settings.url();
  clay.getSettings(event.response, false);
  localStorage.setItem('last-server', settings.url());
  watch.notify(previous !== settings.url() ? keys.KEY_CONFIG_CHANGED : keys.KEY_PHONE_READY, 2);
});
Pebble.addEventListener('appmessage', function (event) {
  var payload = event.payload || {};
  function value(name) { return settings.value(payload, name); }
  var seq = Number(value('KEY_REQUEST_SEQ'));
  if (!(seq > 0)) return;
  if (Number(value('KEY_PROTOCOL_VERSION')) !== 2) {
    return watch.error(seq, new Error('Update the watch app'));
  }
  var request = { seq: seq, type: 'read', revision: Number(value('KEY_REVISION')) };
  sync.ack(Number(value('KEY_ACK_SEQ')) || 0);
  if (value('KEY_ADD_REQUEST') !== undefined) {
    request.type = 'add';
    request.name = String(value('KEY_ADD_REQUEST') || '').replace(/^\s+|\s+$/g, '').replace(/\.$/, '').replace(/\s+$/g, '');
    if (!request.name) request.error = new Error('Item name is empty');
  } else if (value('KEY_TOGGLE_REQUEST') !== undefined) {
    request.type = 'set_checked';
    request.item_id = Number(value('KEY_ITEM_ID'));
    request.checked = Number(value('KEY_ITEM_CHECKED')) === 1 ? 1 : 0;
  } else if (value('KEY_CLEAR_CHECKED_REQUEST') !== undefined) {
    request.type = 'delete_items';
    var bytes = value('KEY_TARGET_IDS') || [];
    request.item_ids = [];
    if (bytes.length % 4 !== 0) request.error = new Error('Invalid clear request');
    for (var i = 0; i < bytes.length; i += 4) {
      request.item_ids.push(bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) | (bytes[i + 3] << 24));
    }
  } else if (value('KEY_FETCH_REQUEST') === undefined) return;
  sync.enqueue(request);
});
