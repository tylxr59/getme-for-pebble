var test = require('node:test');
var fs = require('node:fs');
var path = require('node:path');
var os = require('node:os');
var exec = require('node:child_process').execFileSync;
test('pending journal survives interruption at every write boundary and corrupt newest bank', function () {
  var dir = fs.mkdtempSync(path.join(os.tmpdir(), 'getme-journal-'));
  try {
    var binary = path.join(dir, 'test');
    exec('cc', ['-Wall', '-Werror', '-I', path.join(__dirname, 'fixtures'),
      '-I', path.join(__dirname, '../src/c'), path.join(__dirname, 'fixtures/pending-store.c'),
      path.join(__dirname, '../src/c/pending_store.c'), '-o', binary]);
    exec(binary);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});
