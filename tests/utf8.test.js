'use strict';

var assert = require('node:assert/strict');
var fs = require('node:fs');
var os = require('node:os');
var path = require('node:path');
var childProcess = require('node:child_process');
var test = require('node:test');

test('watch truncation preserves complete UTF-8 characters at every byte boundary', function () {
  var dir = fs.mkdtempSync(path.join(os.tmpdir(), 'getme-utf8-'));
  try {
    var harness = path.join(dir, 'test.c');
    var binary = path.join(dir, 'test');
    fs.writeFileSync(harness, '#include <stdio.h>\n#include <string.h>\n' +
      '#include "util.h"\nint main(int argc, char **argv) {\n' +
      'char name[90]; strncpy(name, argv[1], 89); name[89] = 0;\n' +
      'utf8_trim_partial(name); fputs(name, stdout); return 0; }\n');
    childProcess.execFileSync('cc', ['-Wall', '-Werror',
      '-I', path.resolve(__dirname, '../src/c'), harness,
      path.resolve(__dirname, '../src/c/util.c'), '-o', binary]);
    ['', 'a'.repeat(90)].concat(['é', '€', '🥑'].flatMap(function (character) {
      return [85, 86, 87, 88, 89].map(function (length) {
        return 'a'.repeat(length) + character;
      });
    })).forEach(function (name) {
      var expected = '';
      for (var character of name) {
        if (Buffer.byteLength(expected + character) > 89) break;
        expected += character;
      }
      assert.equal(childProcess.execFileSync(binary, [name], { encoding: 'utf8' }),
        expected);
    });
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
