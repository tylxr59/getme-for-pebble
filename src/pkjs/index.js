var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var messageKeys = require('message_keys');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

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

var requestQueue = [];
var requestActive = false;
// The watch shows changes immediately, so one sync after the queue drains
// is enough to reconcile any number of saved changes.
var syncNeeded = false;
var lastProcessedSeq = 0;

function payloadValue(payload, name) {
  if (payload[name] !== undefined) return payload[name];
  var key = messageKeys[name];
  if (key !== undefined && payload[key] !== undefined) return payload[key];
  if (key !== undefined && payload[String(key)] !== undefined) {
    return payload[String(key)];
  }
  return undefined;
}

function normalizeGetmeUrl(url) {
  url = String(url || '').replace(/^\s+|\s+$/g, '');
  if (!/^https?:\/\//i.test(url)) return '';
  if (url.indexOf('?') === -1 && url.indexOf('#') === -1 &&
      !/\/$/.test(url) && !/\/[^\/]+\.[^\/]+$/.test(url)) {
    url += '/';
  }
  return url;
}

function getConfiguredUrl() {
  // Clay is the settings source of truth. The separate URL is a legacy cache
  // and must not override a newly saved (or deliberately cleared) setting.
  try {
    var claySettings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    if (claySettings &&
        Object.prototype.hasOwnProperty.call(claySettings, 'getmeUrl')) {
      return normalizeGetmeUrl(claySettings.getmeUrl);
    }
  } catch (e) {}
  return normalizeGetmeUrl(localStorage.getItem('getmeUrl') || '');
}

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
  dict[key] = 1;
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
  sendMessage(dict, callback);
}

function postGetme(action, payload, callback) {
  var url = getConfiguredUrl();
  if (!url) return callback(new Error('Set GetMe URL'));

  var req = new XMLHttpRequest();
  var finished = false;
  var timer = setTimeout(function () {
    if (finished) return;
    finished = true;
    try { req.abort(); } catch (e) {}
    callback(new Error('Network timeout'));
  }, 20000);

  function finish(err, data) {
    if (finished) return;
    finished = true;
    clearTimeout(timer);
    callback(err, data);
  }

  req.open('POST', url, true);
  req.setRequestHeader('Content-Type', 'application/json');

  req.onload = function () {
    var data;
    try {
      data = JSON.parse(req.responseText);
    } catch (e) {
      if (req.status < 200 || req.status >= 300) {
        return finish(new Error('HTTP ' + req.status));
      }
      return finish(new Error('Bad server response'));
    }

    if (req.status < 200 || req.status >= 300 || !data.success) {
      return finish(new Error(data.error || ('HTTP ' + req.status)));
    }

    finish(null, data);
  };

  req.onerror = function () {
    finish(new Error('Network error'));
  };

  var body = { action: action };
  Object.keys(payload || {}).forEach(function (key) {
    body[key] = payload[key];
  });
  req.send(JSON.stringify(body));
}

function sendItems(seq, items, callback) {
  if (items.length > MAX_CHECKLIST_ITEMS) {
    return callback(new Error('List has more than 52 items'));
  }

  var index = 0;
  var begin = {};
  begin[KEY_REQUEST_SEQ] = seq;
  begin[KEY_SYNC_BEGIN] = items.length;

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

function fetchAndSendList(seq, callback) {
  postGetme('fetch', {}, function (err, data) {
    if (err) return callback(err);
    if (!data.items || !data.items.length) return sendItems(seq, [], callback);
    sendItems(seq, data.items, callback);
  });
}

function finishRequest() {
  requestActive = false;
  processNextRequest();
}

function syncList() {
  var seq = lastProcessedSeq;
  syncNeeded = false;
  requestActive = true;
  // The sequence tells the watch which requests this list already includes.
  fetchAndSendList(seq, function (err) {
    if (err) return sendError(seq, err, finishRequest);
    finishRequest();
  });
}

function processNextRequest() {
  if (requestActive) return;
  if (!requestQueue.length) {
    if (syncNeeded) syncList();
    return;
  }

  requestActive = true;
  var request = requestQueue.shift();
  lastProcessedSeq = Math.max(lastProcessedSeq, request.seq);

  if (request.type === 'error') {
    sendError(request.seq, request.error, finishRequest);
    return;
  }

  syncNeeded = true;
  if (request.type === 'fetch') {
    finishRequest();
    return;
  }

  var action = request.type === 'clear' ? 'clear_checked' : request.type;
  postGetme(action, request.payload, function (err) {
    if (err) return sendError(request.seq, err, finishRequest);
    finishRequest();
  });
}

function enqueueRequest(request) {
  requestQueue.push(request);
  processNextRequest();
}

// Report invalid requests in order so earlier queued changes are unaffected.
function rejectRequest(seq, error) {
  enqueueRequest({ seq: seq, type: 'error', error: error });
}

Pebble.addEventListener('ready', function () {
  var url = getConfiguredUrl();
  if (url) localStorage.setItem('getmeUrl', url);
  console.log('getme for Pebble JS loaded');
  notifyWatch(KEY_PHONE_READY, 3);
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;

  clay.getSettings(e.response, false);
  var url = getConfiguredUrl();
  if (url) {
    localStorage.setItem('getmeUrl', url);
    notifyWatch(KEY_CONFIG_CHANGED, 2);
  } else {
    localStorage.removeItem('getmeUrl');
  }
});

Pebble.addEventListener('appmessage', function (e) {
  var payload = e.payload || {};
  var seq = Number(payloadValue(payload, 'KEY_REQUEST_SEQ'));
  if (!(seq > 0)) {
    console.log('Ignoring request without a valid sequence');
    return;
  }

  if (payloadValue(payload, 'KEY_FETCH_REQUEST') !== undefined) {
    enqueueRequest({ seq: seq, type: 'fetch', payload: {} });
    return;
  }

  var addName = payloadValue(payload, 'KEY_ADD_REQUEST');
  if (addName !== undefined) {
    addName = String(addName || '').replace(/^\s+|\s+$/g, '');
    // Dictation commonly appends sentence punctuation to grocery names.
    addName = addName.replace(/\.$/, '').replace(/\s+$/g, '');
    if (!addName) return rejectRequest(seq, new Error('Item name is empty'));
    enqueueRequest({ seq: seq, type: 'add', payload: { name: addName } });
    return;
  }

  if (payloadValue(payload, 'KEY_TOGGLE_REQUEST') !== undefined) {
    var itemId = parseInt(payloadValue(payload, 'KEY_ITEM_ID'), 10);
    if (!(itemId > 0)) return rejectRequest(seq, new Error('Invalid item'));
    var checked = payloadValue(payload, 'KEY_ITEM_CHECKED');
    enqueueRequest({
      seq: seq,
      type: 'toggle',
      payload: { id: itemId, checked: Number(checked) === 1 ? 1 : 0 }
    });
    return;
  }

  if (payloadValue(payload, 'KEY_CLEAR_CHECKED_REQUEST') !== undefined) {
    enqueueRequest({ seq: seq, type: 'clear', payload: {} });
  }
});
