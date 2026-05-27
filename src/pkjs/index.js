var Clay = require('@rebble/clay');
var clayConfig = require('./config');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var KEY_FETCH_REQUEST = 0;
var KEY_ADD_REQUEST = 1;
var KEY_TOGGLE_REQUEST = 2;
var KEY_CLEAR_CHECKED_REQUEST = 3;
var KEY_ITEM_ID = 10;
var KEY_ITEM_NAME = 11;
var KEY_ITEM_CHECKED = 12;
var KEY_SYNC_BEGIN = 20;
var KEY_SYNC_ITEM = 21;
var KEY_SYNC_DONE = 22;
var KEY_SYNC_ERROR = 23;
var KEY_STATUS = 24;

function value(payload, numericKey, name) {
  if (payload[numericKey] !== undefined) return payload[numericKey];
  if (payload[String(numericKey)] !== undefined) return payload[String(numericKey)];
  if (name && payload[name] !== undefined) return payload[name];
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

function formEncode(data) {
  var parts = [];
  Object.keys(data).forEach(function (key) {
    if (data[key] === undefined || data[key] === null) return;
    parts.push(encodeURIComponent(key) + '=' + encodeURIComponent(data[key]));
  });
  return parts.join('&');
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
    if (callback) callback();
  }, function () {
    console.log('Failed to send AppMessage: ' + JSON.stringify(dict));
    if (callback) callback();
  });
}

function sendStatus(message) {
  var dict = {};
  dict[KEY_STATUS] = String(message || '').substring(0, 60);
  sendMessage(dict);
}

function sendError(error) {
  var message = error && error.message ? error.message : String(error || 'Sync failed');
  var dict = {};
  dict[KEY_SYNC_ERROR] = message.substring(0, 60);
  sendMessage(dict);
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

  req.open('POST', url, true);
  req.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');

  req.onload = function () {
    if (finished) return;
    finished = true;
    clearTimeout(timer);

    var data;
    try {
      data = JSON.parse(req.responseText);
    } catch (e) {
      return callback(new Error('Bad server response'));
    }

    if (req.status < 200 || req.status >= 300 || !data.success) {
      return callback(new Error(data.error || ('HTTP ' + req.status)));
    }

    callback(null, data);
  };

  req.onerror = function () {
    if (finished) return;
    finished = true;
    clearTimeout(timer);
    callback(new Error('Network error'));
  };

  var body = payload || {};
  body.action = action;
  req.send(formEncode(body));
}

function sendItems(items) {
  var index = 0;
  var begin = {};
  begin[KEY_SYNC_BEGIN] = items.length;

  sendMessage(begin, function () {
    function sendNext() {
      if (index >= items.length) {
        var done = {};
        done[KEY_SYNC_DONE] = 1;
        sendMessage(done);
        return;
      }

      var item = items[index];
      var dict = {};
      dict[KEY_SYNC_ITEM] = index;
      dict[KEY_ITEM_ID] = parseInt(item.id, 10) || 0;
      dict[KEY_ITEM_NAME] = String(item.name || '').substring(0, 89);
      dict[KEY_ITEM_CHECKED] = Number(item.checked) === 1 ? 1 : 0;
      index++;
      sendMessage(dict, sendNext);
    }
    sendNext();
  });
}

function fetchList() {
  sendStatus('Refreshing data');
  postGetme('fetch', {}, function (err, data) {
    if (err) return sendError(err);
    if (!data.items || !data.items.length) return sendItems([]);
    sendItems(data.items);
  });
}

function addItem(name) {
  name = String(name || '').replace(/^\s+|\s+$/g, '');
  if (!name) return;
  sendStatus('Adding');
  postGetme('add', { name: name }, function (err) {
    if (err) return sendError(err);
    fetchList();
  });
}

function toggleItem(id, checked) {
  sendStatus('Saving');
  postGetme('toggle', { id: id, checked: checked ? 1 : 0 }, function (err) {
    if (err) return sendError(err);
    fetchList();
  });
}

function clearChecked() {
  sendStatus('Clearing');
  postGetme('clear_checked', {}, function (err) {
    if (err) return sendError(err);
    fetchList();
  });
}

Pebble.addEventListener('ready', function () {
  console.log('getme for Pebble JS loaded');
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
    fetchList();
  } else {
    localStorage.removeItem('getmeUrl');
    sendError(new Error('Set GetMe URL'));
  }
});

Pebble.addEventListener('appmessage', function (e) {
  var payload = e.payload || {};

  if (value(payload, KEY_FETCH_REQUEST, 'KEY_FETCH_REQUEST') !== undefined) {
    fetchList();
    return;
  }

  var addName = value(payload, KEY_ADD_REQUEST, 'KEY_ADD_REQUEST');
  if (addName !== undefined) {
    addItem(addName);
    return;
  }

  if (value(payload, KEY_TOGGLE_REQUEST, 'KEY_TOGGLE_REQUEST') !== undefined) {
    toggleItem(
      value(payload, KEY_ITEM_ID, 'KEY_ITEM_ID'),
      value(payload, KEY_ITEM_CHECKED, 'KEY_ITEM_CHECKED')
    );
    return;
  }

  if (value(payload, KEY_CLEAR_CHECKED_REQUEST, 'KEY_CLEAR_CHECKED_REQUEST') !== undefined) {
    clearChecked();
  }
});
