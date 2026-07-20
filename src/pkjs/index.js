var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var messageKeys = require('message_keys');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var MAX_CHECKLIST_ITEMS = 52;

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
var KEY_STATUS = messageKeys.KEY_STATUS;

var requestQueue = [];
var requestActive = false;

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
  var raw = localStorage.getItem('getmeUrl') || '';
  if (!raw) {
    try {
      var claySettings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
      raw = claySettings.getmeUrl || '';
    } catch (e) {
      raw = '';
    }
  }
  return normalizeGetmeUrl(raw);
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

function sendStatus(seq, message, callback) {
  var dict = {};
  dict[KEY_REQUEST_SEQ] = seq;
  dict[KEY_STATUS] = String(message || '').substring(0, 60);
  sendMessage(dict, callback);
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
      var itemName = String(item.name || '').substring(0, 89);
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
  if (requestQueue.length) {
    setTimeout(processNextRequest, 0);
  }
}

function finishWithError(seq, error) {
  sendError(seq, error, function () {
    finishRequest();
  });
}

function finishWithList(seq) {
  fetchAndSendList(seq, function (err) {
    if (err) return finishWithError(seq, err);
    finishRequest();
  });
}

function processNextRequest() {
  if (requestActive || !requestQueue.length) return;

  requestActive = true;
  var request = requestQueue.shift();
  var status = request.type === 'fetch' ? 'Refreshing data' :
    request.type === 'add' ? 'Adding' :
    request.type === 'toggle' ? 'Saving' : 'Clearing';

  sendStatus(request.seq, status, function (statusError) {
    if (statusError) {
      finishRequest();
      return;
    }

    if (request.type === 'fetch') {
      finishWithList(request.seq);
      return;
    }

    var action = request.type === 'clear' ? 'clear_checked' : request.type;
    postGetme(action, request.payload, function (err) {
      if (err) return finishWithError(request.seq, err);
      finishWithList(request.seq);
    });
  });
}

function enqueueRequest(request) {
  requestQueue.push(request);
  processNextRequest();
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
    if (!addName) return finishWithError(seq, new Error('Item name is empty'));
    enqueueRequest({ seq: seq, type: 'add', payload: { name: addName } });
    return;
  }

  if (payloadValue(payload, 'KEY_TOGGLE_REQUEST') !== undefined) {
    var itemId = parseInt(payloadValue(payload, 'KEY_ITEM_ID'), 10);
    if (!(itemId > 0)) return finishWithError(seq, new Error('Invalid item'));
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
