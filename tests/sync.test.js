'use strict';
var test = require('node:test');
var assert = require('node:assert/strict');
var createSync = require('../src/pkjs/sync');
var createSettings = require('../src/pkjs/settings');
var createTransport = require('../src/pkjs/watch-transport');
var keys = require('../package.json').pebble.messageKeys;
function storage() {
  var values = {};
  return { getItem: function (key) { return values[key] === undefined ? null : values[key]; },
    setItem: function (key, value) { values[key] = String(value); },
    removeItem: function (key) { delete values[key]; } };
}
function harness(saved, http) {
  var snapshots = [], errors = [];
  var bridge = createSync(saved, function () { return 'https://example/getme/'; }, http, {
    snapshot: function (seq, data, done) { snapshots.push({ seq: seq, data: data }); done(); },
    error: function (seq, error, done) { errors.push({ seq: seq, error: error }); if (done) done(); }
  });
  return { bridge: bridge, errors: errors, snapshots: snapshots };
}
test('phone restart retries the exact prepared operation after response loss', function () {
  var saved = storage(), bodies = [], attempts = 0;
  function http(url, body, done) {
    bodies.push(JSON.stringify(body));
    if (attempts++ === 0) { var error = new Error('Lost'); error.retryable = true; return done(error); }
    done(null, { revision: 4, items: [] });
  }
  var request = { seq: 10, revision: 3, type: 'add', name: 'Milk' };
  var one = harness(saved, http); one.bridge.enqueue(request);
  assert.equal(one.errors[0].error.retryable, true);
  var two = harness(saved, http); two.bridge.enqueue(request);
  assert.equal(bodies[0], bodies[1]);
  assert.equal(two.snapshots.length, 1);
  two.bridge.ack(10);
  assert.deepEqual(JSON.parse(saved.getItem('sync:https://example/getme/')).pending, {});
});
test('queued local edits advance through own revision chain', function () {
  var saved = storage(), revisions = [];
  var app = harness(saved, function (url, body, done) {
    revisions.push(body.revision);
    done(null, { revision: body.revision + 1, items: [] });
  });
  app.bridge.enqueue({ seq: 1, revision: 5, type: 'set_checked', item_id: 1, checked: 1 });
  app.bridge.enqueue({ seq: 2, revision: 5, type: 'set_checked', item_id: 2, checked: 1 });
  assert.deepEqual(revisions, [5, 6]);
});
test('conflict is reported and reconciled without silently replaying edit', function () {
  var requests = [];
  var app = harness(storage(), function (url, body, done) {
    requests.push(body);
    if (body.action === 'read') return done(null, { revision: 8, items: [] });
    var error = new Error('Changed'); error.code = 'conflict'; done(error);
  });
  app.bridge.enqueue({ seq: 1, revision: 3, type: 'add', name: 'Milk' });
  assert.deepEqual(requests.map(function (body) { return body.action; }), ['apply', 'read']);
  assert.equal(requests[0].revision, 3, 'must not adopt a freshly read remote revision before saving');
  assert.equal(app.errors.length, 1);
  assert.equal(app.snapshots[0].data.revision, 8);
});
test('clear sends frozen watch targets even when other items changed', function () {
  var sent;
  var app = harness(storage(), function (url, body, done) {
    sent = body; done(null, { revision: 2, items: [] });
  });
  app.bridge.enqueue({ seq: 1, revision: 1, type: 'delete_items', item_ids: [7, 9] });
  assert.deepEqual(sent.operations[0].item_ids, [7, 9]);
});
test('snapshot explicitly declares omitted items and respects UTF-8 limits', function () {
  var messages = [];
  var transport = createTransport({ sendAppMessage: function (body, done) { messages.push(body); done(); } }, keys);
  var items = Array.from({ length: 60 }, function (_, i) { return { id: i + 1, name: '🥑'.repeat(30), checked: 0 }; });
  transport.snapshot(7, { revision: 12, items: items }, function (error) { assert.equal(error, null); });
  assert.equal(messages[0][keys.KEY_SYNC_BEGIN], 52);
  assert.equal(messages[0][keys.KEY_TOTAL_ITEMS], 60);
  assert.equal(messages[0][keys.KEY_REVISION], 12);
  assert.equal(messages[0][keys.KEY_PROTOCOL_VERSION], 2);
  assert.equal(messages.length, 54);
  assert.equal(messages[1][keys.KEY_ITEM_NAME], '🥑'.repeat(22));
});
test('Clay settings override legacy URL and normalize directory URL', function () {
  var saved = storage(); saved.setItem('getmeUrl', 'http://old/');
  saved.setItem('clay-settings', JSON.stringify({ getmeUrl: 'https://example/getme' }));
  var settings = createSettings(saved, keys);
  assert.equal(settings.url(), 'https://example/getme/');
  saved.setItem('clay-settings', JSON.stringify({ getmeUrl: '' }));
  assert.equal(settings.url(), '');
  assert.equal(settings.value({ '4': 123 }, 'KEY_REQUEST_SEQ'), 123);
});
test('changing endpoints never forwards queued old-server edits to the new server', function () {
  var saved = storage(), url = 'https://old/', calls = [], pending;
  var watch = { snapshot: function (seq, data, done) { done(); }, error: function (seq, error, done) { if (done) done(); } };
  var sync = createSync(saved, function () { return url; }, function (target, body, done) {
    calls.push(target); pending = done;
  }, watch);
  sync.enqueue({ seq: 1, revision: 0, type: 'add', name: 'Old' });
  sync.enqueue({ seq: 2, revision: 0, type: 'add', name: 'Also old' });
  url = 'https://new/';
  pending(null, { revision: 1, items: [] });
  assert.deepEqual(calls, ['https://old/']);
  sync.enqueue({ seq: 3, revision: 0, type: 'read' });
  assert.deepEqual(calls, ['https://old/', 'https://new/']);
});

test('validation errors cannot overtake an earlier in-flight save', function () {
  var pending, errors = [];
  var sync = createSync(storage(), function () { return 'https://example/'; },
    function (url, body, done) {
      if (body.action === 'read') return done(null, { revision: 2, items: [] });
      pending = done;
    }, {
      snapshot: function (seq, data, done) { done(); },
      error: function (seq, error, done) { errors.push(seq); done(); }
    });
  sync.enqueue({ seq: 1, revision: 1, type: 'add', name: 'Milk' });
  sync.enqueue({ seq: 2, revision: 1, type: 'add', error: new Error('Empty') });
  assert.deepEqual(errors, []);
  pending(null, { revision: 2, items: [] });
  assert.deepEqual(errors, [2]);
});
