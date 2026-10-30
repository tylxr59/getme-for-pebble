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
          '{"success":true,"version":1,"revision":1,"items":[]}' : '<!DOCTYPE html><html></html>';
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
    if (name === './http-client') return function () { return require('../src/pkjs/http-client')(context.XMLHttpRequest); };
    if (name.indexOf('./') === 0) return require('../src/pkjs/' + name.slice(2));
    throw new Error('Unexpected module: ' + name);
  };
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../src/pkjs/index.js'), 'utf8'), context);
  return { context: context, storage: storage, listeners: listeners,
    messages: messages, requests: requests };
}

var url = 'https://www.westfall.cc/getme/';

function fetchList(app, seq) {
  app.listeners.appmessage({ payload: { KEY_FETCH_REQUEST: 1, KEY_REQUEST_SEQ: seq,
    KEY_PROTOCOL_VERSION: 2, KEY_REVISION: 0, KEY_ACK_SEQ: 0 } });
}
test('HTML redirects preserve pending actions as a retryable failure', function () {
  var app = bridge({ getmeUrl: 'http://example/getme/' });
  fetchList(app, 1);
  assert.ok(app.messages.some(function (m) { return m[keys.KEY_RETRYABLE] === 1; }));
});
test('Clay saves HTTPS settings and the versioned read syncs successfully', function () {
  var app = bridge({ getmeUrl: 'http://old/getme/' });
  app.listeners.webviewclosed({ response: encodeURIComponent(JSON.stringify({ getmeUrl: { value: url } })) });
  fetchList(app, 1);
  assert.equal(app.requests[0].url, url);
  assert.equal(app.requests[0].body.action, 'read');
  assert.equal(app.requests[0].body.version, 1);
  assert.ok(app.messages.some(function (m) { return m[keys.KEY_SYNC_DONE] === 1; }));
});
test('incompatible watch protocol is rejected before network activity', function () {
  var app = bridge({ getmeUrl: url });
  app.listeners.appmessage({ payload: { KEY_FETCH_REQUEST: 1, KEY_REQUEST_SEQ: 1 } });
  assert.equal(app.requests.length, 0);
  assert.ok(app.messages.some(function (m) { return m[keys.KEY_SYNC_ERROR] === 'Update the watch app'; }));
});
test('ready handshake detects a server changed while the watch was closed', function () {
  var app = bridge({ getmeUrl: url, 'last-server': 'https://old/getme/' });
  app.listeners.ready();
  assert.ok(app.messages.some(function (m) { return m[keys.KEY_CONFIG_CHANGED] === 2; }));
});
test('dictation strips trailing sentence punctuation before submitting an operation', function () {
  var app = bridge({ getmeUrl: url });
  app.listeners.appmessage({ payload: { KEY_ADD_REQUEST: ' Milk. ', KEY_REQUEST_SEQ: 1,
    KEY_PROTOCOL_VERSION: 2, KEY_REVISION: 0 } });
  assert.equal(app.requests[0].body.operations[0].name, 'Milk');
});
