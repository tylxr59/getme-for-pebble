'use strict';

var assert = require('node:assert/strict');
var fs = require('node:fs');
var path = require('node:path');
var vm = require('node:vm');
var test = require('node:test');
var keys = require('../package.json').pebble.messageKeys;
var clayPath = path.join(path.dirname(require.resolve('@rebble/clay')), 'src/js/index.js');

function bridge(saved) {
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
