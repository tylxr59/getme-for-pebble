'use strict';

var assert = require('node:assert/strict');
var fs = require('node:fs');
var path = require('node:path');
var vm = require('node:vm');
var test = require('node:test');
var keys = require('../package.json').pebble.messageKeys;
var clayPath = path.join(path.dirname(require.resolve('@rebble/clay')), 'src/js/index.js');

function bridge(saved, respond) {
  var storage = Object.assign({}, saved);
  var listeners = {};
  var messages = [];
  var requests = [];
  var context = vm.createContext({
    console: console,
    setTimeout: setTimeout,
    clearTimeout: clearTimeout,
    localStorage: {
      getItem: function (key) { return storage[key] || null; },
      setItem: function (key, value) { storage[key] = String(value); },
      removeItem: function (key) { delete storage[key]; }
    },
    Pebble: {
      addEventListener: function (event, callback) { listeners[event] = callback; },
      sendAppMessage: function (message, success) { messages.push(message); success(); }
    },
    XMLHttpRequest: function () {
      this.open = function (method, url) { this.url = url; };
      this.setRequestHeader = function () {};
      this.send = function (body) {
        requests.push({ url: this.url, body: JSON.parse(body) });
        if (respond) return respond(this, JSON.parse(body));
        // An HTTP redirect changes a POST to GET, returning the web page.
        this.status = 200;
        this.responseText = /^https:\/\//.test(this.url) ?
          '{"success":true,"items":[]}' : '<!DOCTYPE html><html></html>';
        this.onload();
      };
    }
  });
  // Exercise Clay's real settings decoding and storage behavior.
  var clayModule = { exports: {} };
  vm.runInContext('(function(require, module, exports) {' +
    fs.readFileSync(clayPath, 'utf8') + '\n})', context)(function (name) {
    if (name === 'message_keys') return keys;
    throw new Error('Unexpected Clay module: ' + name);
  }, clayModule, clayModule.exports);
  context.require = function (name) {
    if (name === '@rebble/clay') return clayModule.exports;
    if (name === 'message_keys') return keys;
    if (name === './config') return require('../src/pkjs/config');
    throw new Error('Unexpected module: ' + name);
  };
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../src/pkjs/index.js'), 'utf8'), context);
  return { context: context, storage: storage, listeners: listeners,
    messages: messages, requests: requests };
}

var url = 'https://www.westfall.cc/getme/';

test('an HTML response after an HTTP redirect produces the reported error', function () {
  var app = bridge({ getmeUrl: 'http://www.westfall.cc/getme/' });
  app.listeners.appmessage({ payload: { KEY_FETCH_REQUEST: 1, KEY_REQUEST_SEQ: 1 } });
  assert.ok(app.messages.some(function (message) {
    return message[keys.KEY_SYNC_ERROR] === 'Bad server response';
  }));
});

test('saving an HTTPS URL replaces the cached HTTP URL and syncs successfully', function () {
  var app = bridge({ getmeUrl: 'http://www.westfall.cc/getme/' });
  app.listeners.webviewclosed({ response: encodeURIComponent(JSON.stringify({
    getmeUrl: { value: url }
  })) });
  app.listeners.appmessage({ payload: { KEY_FETCH_REQUEST: 1, KEY_REQUEST_SEQ: 1 } });
  assert.equal(app.storage.getmeUrl, url);
  assert.equal(app.requests[0].url, url);
  assert.equal(app.requests[0].body.action, 'fetch');
  assert.ok(app.messages.some(function (message) { return message[keys.KEY_SYNC_DONE] === 1; }));
  assert.ok(!app.messages.some(function (message) { return message[keys.KEY_SYNC_ERROR]; }));
});

test('existing Clay settings take precedence over a stale URL on startup', function () {
  var app = bridge({ getmeUrl: 'http://old.example/getme/',
    'clay-settings': JSON.stringify({ getmeUrl: url }) });
  app.listeners.ready();
  assert.equal(app.storage.getmeUrl, url);
});

test('clearing or invalidating settings does not restore the old URL', function () {
  ['', 'not a URL'].forEach(function (value) {
    var app = bridge({ getmeUrl: url });
    app.listeners.webviewclosed({ response: JSON.stringify({ getmeUrl: { value: value } }) });
    assert.equal(app.storage.getmeUrl, undefined);
    app.listeners.appmessage({ payload: { KEY_FETCH_REQUEST: 1, KEY_REQUEST_SEQ: 1 } });
    assert.equal(app.requests.length, 0);
    assert.ok(app.messages.some(function (message) {
      return message[keys.KEY_SYNC_ERROR] === 'Set GetMe URL';
    }));
  });
});

test('legacy URL storage still works without a Clay URL', function () {
  [undefined, '{}', '{broken'].forEach(function (settings) {
    var app = bridge({ getmeUrl: url, 'clay-settings': settings });
    assert.equal(app.context.getConfiguredUrl(), url);
  });
});

test('directory URLs receive a trailing slash before making a request', function () {
  var app = bridge({ 'clay-settings': JSON.stringify({ getmeUrl: url.slice(0, -1) }) });
  app.listeners.appmessage({ payload: { KEY_FETCH_REQUEST: 1, KEY_REQUEST_SEQ: 1 } });
  assert.equal(app.requests[0].url, url);
});

test('synced names fit the UTF-8 byte limit without splitting characters', function () {
  var app = bridge({ getmeUrl: url });
  ['a'.repeat(90), 'é'.repeat(45), '€'.repeat(30), '🥑'.repeat(23),
    'a'.repeat(88) + 'é', 'a'.repeat(87) + '🥑',
    'a'.repeat(85) + '🥑', 'Milk.'].forEach(function (name) {
    var error;
    app.context.sendItems(1, [{ id: 1, name: name, checked: 0 }], function (err) {
      error = err;
    });
    assert.equal(error, null);
    var message = app.messages.filter(function (entry) {
      return entry[keys.KEY_ITEM_NAME] !== undefined;
    }).pop();
    var actual = message[keys.KEY_ITEM_NAME];
    var expected = '';
    for (var character of name) {
      if (Buffer.byteLength(expected + character, 'utf8') > 89) break;
      expected += character;
    }
    assert.equal(actual, expected);
  });
});

test('dictated additions lose only their trailing period and surrounding whitespace', function () {
  [' Milk. ', 'Milk', 'St. John’s wort.', '.'].forEach(function (name) {
    var app = bridge({ getmeUrl: url });
    app.listeners.appmessage({ payload: {
      KEY_ADD_REQUEST: name, KEY_REQUEST_SEQ: 1
    } });
    if (name === '.') {
      assert.equal(app.requests.length, 0);
      assert.ok(app.messages.some(function (message) {
        return message[keys.KEY_SYNC_ERROR] === 'Item name is empty';
      }));
    } else {
      assert.equal(app.requests[0].body.action, 'add');
      assert.equal(app.requests[0].body.name,
        name === 'St. John’s wort.' ? 'St. John’s wort' : 'Milk');
    }
  });
});

function deferredServer() {
  var server = { pending: [], items: [{ id: 1, name: 'Milk', checked: 0 },
    { id: 2, name: 'Eggs', checked: 0 }], fail: {} };
  server.respond = function (req, body) {
    server.pending.push(function () {
      var result = { success: true };
      if (body.action === 'toggle') {
        if (server.fail[body.id]) {
          result = { success: false, error: 'Save failed' };
        } else {
          server.items.forEach(function (item) {
            if (item.id === body.id) item.checked = body.checked;
          });
        }
      } else if (body.action === 'fetch') {
        result.items = JSON.parse(JSON.stringify(server.items));
      }
      req.status = 200;
      req.responseText = JSON.stringify(result);
      req.onload();
    });
  };
  server.flush = function () {
    while (server.pending.length) server.pending.shift()();
  };
  return server;
}

function toggle(app, seq, id, checked) {
  app.listeners.appmessage({ payload: { KEY_TOGGLE_REQUEST: 1,
    KEY_REQUEST_SEQ: seq, KEY_ITEM_ID: id, KEY_ITEM_CHECKED: checked } });
}

test('toggles queued during a save are applied in order with one sync', function () {
  var server = deferredServer();
  var app = bridge({ getmeUrl: url }, server.respond);
  toggle(app, 1, 1, 1);
  toggle(app, 2, 2, 1);
  toggle(app, 3, 1, 0);
  assert.equal(app.messages.length, 0, 'no status round trips before saving');
  server.flush();

  assert.deepEqual(app.requests.map(function (request) {
    return request.body.action + ':' + (request.body.id || '') + ':' +
      (request.body.checked === undefined ? '' : request.body.checked);
  }), ['toggle:1:1', 'toggle:2:1', 'toggle:1:0', 'fetch::']);
  var begins = app.messages.filter(function (message) {
    return message[keys.KEY_SYNC_BEGIN] !== undefined;
  });
  assert.equal(begins.length, 1);
  assert.equal(begins[0][keys.KEY_REQUEST_SEQ], 3);
  var synced = app.messages.filter(function (message) {
    return message[keys.KEY_SYNC_ITEM] === 1;
  }).map(function (message) { return message[keys.KEY_ITEM_CHECKED]; });
  assert.deepEqual(synced, [0, 1]);
});

test('a failed toggle reports its own sequence and still reconciles', function () {
  var server = deferredServer();
  server.fail[1] = true;
  var app = bridge({ getmeUrl: url }, server.respond);
  toggle(app, 5, 1, 1);
  toggle(app, 6, 2, 1);
  server.flush();

  var errors = app.messages.filter(function (message) {
    return message[keys.KEY_SYNC_ERROR] !== undefined;
  });
  assert.equal(errors.length, 1);
  assert.equal(errors[0][keys.KEY_REQUEST_SEQ], 5);
  assert.equal(errors[0][keys.KEY_SYNC_ERROR], 'Save failed');
  assert.ok(app.messages.some(function (message) {
    return message[keys.KEY_SYNC_DONE] === 1 && message[keys.KEY_REQUEST_SEQ] === 6;
  }));
});

test('invalid requests are reported in order without interrupting saves', function () {
  var server = deferredServer();
  var app = bridge({ getmeUrl: url }, server.respond);
  toggle(app, 1, 1, 1);
  toggle(app, 2, 0, 1);
  assert.equal(app.messages.length, 0);
  server.flush();

  var errorIndex = app.messages.findIndex(function (message) {
    return message[keys.KEY_SYNC_ERROR] === 'Invalid item';
  });
  var beginIndex = app.messages.findIndex(function (message) {
    return message[keys.KEY_SYNC_BEGIN] !== undefined;
  });
  assert.ok(errorIndex >= 0 && errorIndex < beginIndex);
  assert.equal(app.messages[beginIndex][keys.KEY_REQUEST_SEQ], 2);
  assert.equal(app.requests.filter(function (request) {
    return request.body.action === 'toggle';
  }).length, 1);
});
