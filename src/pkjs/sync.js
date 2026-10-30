// Watch owns the durable outbox. Phone owns immutable HTTP envelopes and the
// revision chain produced by its own writes (never revisions read from others).
module.exports = function (storage, getUrl, http, watch) {
  var queue = [];
  var active = false;
  var blocked = false;
  var client = storage.getItem('sync-client');
  if (!client) {
    client = 'pebble-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2);
    storage.setItem('sync-client', client);
  }
  function load(url) {
    return JSON.parse(storage.getItem('sync:' + url) || '{"pending":{},"chain":null}');
  }
  function save(url, state) { storage.setItem('sync:' + url, JSON.stringify(state)); }
  function process() {
    if (active || !queue.length || blocked) return;
    active = true;
    var request = queue.shift();
    var url = request.url;
    var state;
    function complete(error, data) {
      function finish() { active = false; process(); }
      if (url !== getUrl()) return finish();
      if (error) {
        if (error.retryable) { blocked = true; queue = []; }
        return watch.error(request.seq, error, function () {
          if (!error.retryable && url) {
            // Reconcile after a definitive rejection; never auto-replay it.
            return http(url, { version: 1, action: 'read' }, function (readError, latest) {
              if (readError) return finish();
              watch.snapshot(request.seq, latest, finish);
            });
          }
          finish();
        });
      }
      watch.snapshot(request.seq, data, function (transferError) {
        if (!transferError) return finish();
        transferError.retryable = true;
        blocked = true;
        queue = [];
        watch.error(request.seq, transferError, finish);
      });
    }
    if (url !== getUrl()) { active = false; process(); return; }
    if (!url) return complete(new Error('Set GetMe URL'));
    if (request.error) return complete(request.error);
    if (request.type === 'read') return http(url, { version: 1, action: 'read' }, complete);
    try { state = load(url); }
    catch (error) { return complete(new Error('Invalid pending operation storage')); }
    var body = state.pending[request.seq];
    if (!body) {
      var op = { id: client + ':' + request.seq, type: request.type };
      if (request.type === 'add') op.name = request.name;
      if (request.type === 'set_checked') { op.item_id = request.item_id; op.checked = request.checked; }
      if (request.type === 'delete_items') op.item_ids = request.item_ids;
      var revision = request.revision;
      if (state.chain && revision >= state.chain.start && revision <= state.chain.end) revision = state.chain.end;
      body = { version: 1, action: 'apply', revision: revision, operations: [op] };
      state.pending[request.seq] = body;
      try { save(url, state); }
      catch (error) { return complete(new Error('Cannot store pending operation')); }
    }
    http(url, body, function (error, data) {
      if (error) {
        if (!error.retryable) {
          delete state.pending[request.seq];
          state.chain = null;
          save(url, state);
        }
        return complete(error);
      }
      // Duplicate responses can include intervening remote writes. Only extend
      // the local chain across exactly one revision; otherwise force a conflict.
      if (data.revision === body.revision + 1) {
        var start = state.chain && state.chain.end === body.revision ? state.chain.start : body.revision;
        state.chain = { start: start, end: data.revision };
      } else state.chain = null;
      // Keep envelope until explicit watch acknowledgment, including across restarts.
      try { save(url, state); }
      catch (error) { error.retryable = true; return complete(error); }
      complete(null, data);
    });
  }
  return {
    ack: function (seq) {
      var url = getUrl();
      if (!url || !seq) return;
      var state = load(url);
      Object.keys(state.pending).forEach(function (key) {
        if (Number(key) <= seq) delete state.pending[key];
      });
      save(url, state);
    },
    enqueue: function (request) {
      blocked = false;
      request.url = getUrl();
      queue.push(request);
      process();
    }
  };
};
