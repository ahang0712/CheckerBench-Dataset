# Patch-overlapping JavaScript context before the fix

## `lib/grunt/file.js` (changed lines (244, 245, 249, 250))

```javascript
'use strict';

var grunt = require('../grunt');

// Nodejs libs.
var fs = require('fs');
var path = require('path');

// The module to be exported.
var file = module.exports = {};

// External libs.
file.glob = require('glob');
file.minimatch = require('minimatch');
file.findup = require('findup-sync');
var YAML = require('js-yaml');
var rimraf = require('rimraf');
var iconv = require('iconv-lite');
var mkdirp = require('mkdirp').sync;

// Windows?
var win32 = process.platform === 'win32';

// Normalize \\ paths to / paths.
var unixifyPath = function(filepath) {
  if (win32) {
    return filepath.replace(/\\/g, '/');
  } else {
    return filepath;
  }
};

// Change the current base path (ie, CWD) to the specified path.
file.setBase = function() {
  var dirpath = path.join.apply(path, arguments);
  process.chdir(dirpath);
};

// Process specified wildcard glob patterns or filenames against a
// callback, excluding and uniquing files in the result set.
var processPatterns = function(patterns, fn) {
  // Filepaths to return.
  var result = [];
  // Iterate over flattened patterns array.
  grunt.util._.flattenDeep(patterns).forEach(function(pattern) {
    // If the first character is ! it should be omitted
    var exclusion = pattern.indexOf('!') === 0;
    // If the pattern is an exclusion, remove the !
    if (exclusion) { pattern = pattern.slice(1); }
    // Find all matching files for this pattern.
    var matches = fn(pattern);
    if (exclusion) {
      // If an exclusion, remove matching files.
      result = grunt.util._.difference(result, matches);
    } else {
      // Otherwise add matching files.
      result = grunt.util._.union(result, matches);
    }
  });
  return result;
};

// Match a filepath or filepaths against one or more wildcard patterns. Returns
// all matching filepaths.
file.match = function(options, patterns, filepaths) {
  if (grunt.util.kindOf(options) !== 'object') {
    filepaths = patterns;
    patterns = options;
    options = {};
  }
  // Return empty set if either patterns or filepaths was omitted.
  if (patterns == null || filepaths == null) { return []; }
  // Normalize patterns and filepaths to arrays.
  if (!Array.isArray(patterns)) { patterns = [patterns]; }
  if (!Array.isArray(filepaths)) { filepaths = [filepaths]; }
  // Return empty set if there are no patterns or filepaths.
  if (patterns.length === 0 || filepaths.length === 0) { return []; }
  // Return all matching filepaths.
  return processPatterns(patterns, function(pattern) {
    return file.minimatch.match(filepaths, pattern, options);
  });
};

// Match a filepath or filepaths against one or more wildcard patterns. Returns
// true if any of the patterns match.
file.isMatch = function() {
  return file.match.apply(file, arguments).length > 0;
};

// Return an array of all file paths that match the given wildcard patterns.
file.expand = function() {
  var args = grunt.util.toArray(arguments);
  // If the first argument is an options object, save those options to pass
  // into the file.glob.sync method.
  var options = grunt.util.kindOf(args[0]) === 'object' ? args.shift() : {};
  // Use the first argument if it's an Array, otherwise convert the arguments
  // object to an array and use that.
  var patterns = Array.isArray(args[0]) ? args[0] : args;
  // Return empty set if there are no patterns or filepaths.
  if (patterns.length === 0) { return []; }
  // Return all matching filepaths.
  var matches = processPatterns(patterns, function(pattern) {
    // Find all matching files for this pattern.
    return file.glob.sync(pattern, options);
  });
  // Filter result set?
  if (options.filter) {
    matches = matches.filter(function(filepath) {
      filepath = path.join(options.cwd || '', filepath);
      try {
        if (typeof options.filter === 'function') {
          return options.filter(filepath);
        } else {
          // If the file is of the right type and exists, this should work.
          return fs.statSync(filepath)[options.filter]();
        }
      } catch (e) {
        // Otherwise, it's probably not the right type.
        return false;
      }
    });
  }
  return matches;
};

var pathSeparatorRe = /[\/\\]/g;

// The "ext" option refers to either everything after the first dot (default)
// or everything after the last dot.
var extDotRe = {
  first: /(\.[^\/]*)?$/,
  last: /(\.[^\/\.]*)?$/,
};

// Build a multi task "files" object dynamically.
file.expandMapping = function(patterns, destBase, options) {
  options = grunt.util._.defaults({}, options, {
    extDot: 'first',
    rename: function(destBase, destPath) {
      return path.join(destBase || '', destPath);
    }
  });
  var files = [];
  var fileByDest = {};
  // Find all files matching pattern, using passed-in options.
  file.expand(options, patterns).forEach(function(src) {
    var destPath = src;
    // Flatten?
    if (options.flatten) {
      destPath = path.basename(destPath);
    }
    // Change the extension?
    if ('ext' in options) {
      destPath = destPath.replace(extDotRe[options.extDot], options.ext);
    }
    // Generate destination filename.
    var dest = options.rename(destBase, destPath, options);
    // Prepend cwd to src path if necessary.
    if (options.cwd) { src = path.join(options.cwd, src); }
    // Normalize filepaths to be unix-style.
    dest = dest.replace(pathSeparatorRe, '/');
    src = src.replace(pathSeparatorRe, '/');
    // Map correct src path to dest path.
    if (fileByDest[dest]) {
      // If dest already exists, push this src onto that dest's src array.
      fileByDest[dest].src.push(src);
    } else {
      // Otherwise create a new src-dest file mapping object.
      files.push({
        src: [src],
        dest: dest,
      });
      // And store a reference for later use.
      fileByDest[dest] = files[files.length - 1];
    }
  });
  return files;
};

// Like mkdir -p. Create a directory and any intermediary directories.
file.mkdir = function(dirpath, mode) {
  if (grunt.option('no-write')) { return; }
  try {
    mkdirp(dirpath, { mode: mode });
  } catch (e) {
    throw grunt.util.error('Unable to create directory "' + dirpath + '" (Error code: ' + e.code + ').', e);
  }
};

// Recurse into a directory, executing callback for each file.
file.recurse = function recurse(rootdir, callback, subdir) {
  var abspath = subdir ? path.join(rootdir, subdir) : rootdir;
  fs.readdirSync(abspath).forEach(function(filename) {
    var filepath = path.join(abspath, filename);
    if (fs.statSync(filepath).isDirectory()) {
      recurse(rootdir, callback, unixifyPath(path.join(subdir || '', filename || '')));
    } else {
      callback(unixifyPath(filepath), rootdir, subdir, filename);
    }
  });
};

// The default file encoding to use.
file.defaultEncoding = 'utf8';
// Whether to preserve the BOM on file.read rather than strip it.
file.preserveBOM = false;

// Read a file, return its contents.
file.read = function(filepath, options) {
  if (!options) { options = {}; }
  var contents;
  grunt.verbose.write('Reading ' + filepath + '...');
  try {
    contents = fs.readFileSync(String(filepath));
    // If encoding is not explicitly null, convert from encoded buffer to a
    // string. If no encoding was specified, use the default.
    if (options.encoding !== null) {
      contents = iconv.decode(contents, options.encoding || file.defaultEncoding, {stripBOM: !file.preserveBOM});
    }
    grunt.verbose.ok();
    return contents;
  } catch (e) {
    grunt.verbose.error();
    throw grunt.util.error('Unable to read "' + filepath + '" file (Error code: ' + e.code + ').', e);
  }
};

// Read a file, parse its contents, return an object.
file.readJSON = function(filepath, options) {
  var src = file.read(filepath, options);
  var result;
  grunt.verbose.write('Parsing ' + filepath + '...');
  try {
    result = JSON.parse(src);
    grunt.verbose.ok();
    return result;
  } catch (e) {
    grunt.verbose.error();
    throw grunt.util.error('Unable to parse "' + filepath + '" file (' + e.message + ').', e);
  }
};

// Read a YAML file, parse its contents, return an object.
file.readYAML = function(filepath, options) {
  var src = file.read(filepath, options);
  var result;
  grunt.verbose.write('Parsing ' + filepath + '...');
  try {
    result = YAML.load(src);
    grunt.verbose.ok();
    return result;
  } catch (e) {
    grunt.verbose.error();
    throw grunt.util.error('Unable to parse "' + filepath + '" file (' + e.message + ').', e);
  }
};

// Write a file.
file.write = function(filepath, contents, options) {
  if (!options) { options = {}; }
  var nowrite = grunt.option('no-write');
  grunt.verbose.write((nowrite ? 'Not actually writing ' : 'Writing ') + filepath + '...');
  // Create path, if necessary.
  file.mkdir(path.dirname(filepath));
  try {
    // If contents is already a Buffer, don't try to encode it. If no encoding
    // was specified, use the default.
    if (!Buffer.isBuffer(contents)) {
      contents = iconv.encode(contents, options.encoding || file.defaultEncoding);
    }
    // Actually write file.
    if (!nowrite) {
      fs.writeFileSync(filepath, contents, 'mode' in options ? {mode: options.mode} : {});
    }
    grunt.verbose.ok();
    return true;
  } catch (e) {
    grunt.verbose.error();
    throw grunt.util.error('Unable to write "' + filepath + '" file (Error code: ' + e.code + ').', e);
  }
};

// Read a file, optionally processing its content, then write the output.
// Or read a directory, recursively creating directories, reading files,
// processing content, writing output.
file.copy = function copy(srcpath, destpath, options) {
  if (file.isDir(srcpath)) {
    // Copy a directory, recursively.
    // Explicitly create new dest directory.
    file.mkdir(destpath);
    // Iterate over all sub-files/dirs, recursing.
    fs.readdirSync(srcpath).forEach(function(filepath) {
      copy(path.join(srcpath, filepath), path.join(destpath, filepath), options);
    });
  } else {
    // Copy a single file.
    file._copy(srcpath, destpath, options);
  }
};

// Read a file, optionally processing its content, then write the output.
file._copy = function(srcpath, destpath, options) {
  if (!options) { options = {}; }
  // If a process function was specified, and noProcess isn't true or doesn't
  // match the srcpath, process the file's source.
  var process = options.process && options.noProcess !== true &&
    !(options.noProcess && file.isMatch(options.noProcess, srcpath));
  // If the file will be processed, use the encoding as-specified. Otherwise,
  // use an encoding of null to force the file to be read/written as a Buffer.
  var readWriteOptions = process ? options : {encoding: null};
  // Actually read the file.
  var contents = file.read(srcpath, readWriteOptions);
  if (process) {
    grunt.verbose.write('Processing source...');
    try {
      contents = options.process(contents, srcpath, destpath);
      grunt.verbose.ok();
    } catch (e) {
      grunt.verbose.error();
      throw grunt.util.error('Error while processing "' + srcpath + '" file.', e);
    }
  }
  // Abort copy if the process function returns false.
  if (contents === false) {
    grunt.verbose.writeln('Write aborted.');
  } else {
    file.write(destpath, contents, readWriteOptions);
  }
};

// Delete folders and files recursively
file.delete = function(filepath, options) {
  filepath = String(filepath);

  var nowrite = grunt.option('no-write');
  if (!options) {
    options = {force: grunt.option('force') || false};
  }

  grunt.verbose.write((nowrite ? 'Not actually deleting ' : 'Deleting ') + filepath + '...');

  if (!file.exists(filepath)) {
    grunt.verbose.error();
    grunt.log.warn('Cannot delete nonexistent file.');
    return false;
  }

  // Only delete cwd or outside cwd if --force enabled. Be careful, people!
  if (!options.force) {
    if (file.isPathCwd(filepath)) {
      grunt.verbose.error();
      grunt.fail.warn('Cannot delete the current working directory.');
      return false;
    } else if (!file.isPathInCwd(filepath)) {
      grunt.verbose.error();
      grunt.fail.warn('Cannot delete files outside the current working directory.');
      return false;
    }
  }

  try {
    // Actually delete. Or not.
    if (!nowrite) {
      rimraf.sync(filepath);
    }
    grunt.verbose.ok();
    return true;
  } catch (e) {
    grunt.verbose.error();
    throw grunt.util.error('Unable to delete "' + filepath + '" file (' + e.message + ').', e);
  }
};

// True if the file path exists.
file.exists = function() {
  var filepath = path.join.apply(path, arguments);
  return fs.existsSync(filepath);
};

// True if the file is a symbolic link.
file.isLink = function() {
  var filepath = path.join.apply(path, arguments);
  try {
    return fs.lstatSync(filepath).isSymbolicLink();
  } catch (e) {
    if (e.code === 'ENOENT') {
      // The file doesn't exist, so it's not a symbolic link.
      return false;
    }
    throw grunt.util.error('Unable to read "' + filepath + '" file (Error code: ' + e.code + ').', e);
  }
};

// True if the path is a directory.
file.isDir = function() {
  var filepath = path.join.apply(path, arguments);
  return file.exists(filepath) && fs.statSync(filepath).isDirectory();
};

// True if the path is a file.
file.isFile = function() {
  var filepath = path.join.apply(path, arguments);
  return file.exists(filepath) && fs.statSync(filepath).isFile();
};

// Is a given file path absolute?
file.isPathAbsolute = function() {
  var filepath = path.join.apply(path, arguments);
  return path.isAbsolute(filepath);
};

// Do all the specified paths refer to the same path?
file.arePathsEquivalent = function(first) {
  first = path.resolve(first);
  for (var i = 1; i < arguments.length; i++) {
    if (first !== path.resolve(arguments[i])) { return false; }
  }
  return true;
};

// Are descendant path(s) contained within ancestor path? Note: does not test
// if paths actually exist.
file.doesPathContain = function(ancestor) {
  ancestor = path.resolve(ancestor);
  var relative;
  for (var i = 1; i < arguments.length; i++) {
    relative = path.relative(path.resolve(arguments[i]), ancestor);
    if (relative === '' || /\w+/.test(relative)) { return false; }
  }
  return true;
};

// Test to see if a filepath is the CWD.
file.isPathCwd = function() {
  var filepath = path.join.apply(path, arguments);
  try {
    return file.arePathsEquivalent(fs.realpathSync(process.cwd()), fs.realpathSync(filepath));
  } catch (e) {
    return false;
  }
};

// Test to see if a filepath is contained within the CWD.
file.isPathInCwd = function() {
  var filepath = path.join.apply(path, arguments);
  try {
    return file.doesPathContain(fs.realpathSync(process.cwd()), fs.realpathSync(filepath));
  } catch (e) {
    return false;
  }
};
```

## `test/grunt/file_test.js` (changed lines (455, 456, 458, 459))

```javascript
'use strict';

var grunt = require('../../lib/grunt');

var fs = require('fs');
var path = require('path');

var Tempfile = require('temporary/lib/file');
var Tempdir = require('temporary/lib/dir');

var win32 = process.platform === 'win32';

var tmpdir = new Tempdir();
try {
  fs.symlinkSync(path.resolve('test/fixtures/octocat.png'), path.join(tmpdir.path, 'octocat.png'), 'file');
  fs.symlinkSync(path.resolve('test/fixtures/expand'), path.join(tmpdir.path, 'expand'), 'dir');
} catch (err) {
  console.error('** ERROR: Cannot create symbolic links; link-related tests will fail.');
  if (win32) {
    console.error('** Tests must be run with Administrator privileges on Windows.');
  }
}

exports['file.match'] = {
  'empty set': function(test) {
    test.expect(12);
    // Should return empty set if a required argument is missing or an empty set.
    test.deepEqual(grunt.file.match(null, null), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, null, null), [], 'should return empty set.');
    test.deepEqual(grunt.file.match(null, 'foo.js'), [], 'should return empty set.');
    test.deepEqual(grunt.file.match('*.js', null), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, null, 'foo.js'), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, '*.js', null), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, [], 'foo.js'), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, '*.js', []), [], 'should return empty set.');
    test.deepEqual(grunt.file.match(null, ['foo.js']), [], 'should return empty set.');
    test.deepEqual(grunt.file.match(['*.js'], null), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, null, ['foo.js']), [], 'should return empty set.');
    test.deepEqual(grunt.file.match({}, ['*.js'], null), [], 'should return empty set.');
    test.done();
  },
  'basic matching': function(test) {
    test.expect(6);
    test.deepEqual(grunt.file.match('*.js', 'foo.js'), ['foo.js'], 'should match correctly.');
    test.deepEqual(grunt.file.match('*.js', ['foo.js']), ['foo.js'], 'should match correctly.');
    test.deepEqual(grunt.file.match('*.js', ['foo.js', 'bar.css']), ['foo.js'], 'should match correctly.');
    test.deepEqual(grunt.file.match(['*.js', '*.css'], 'foo.js'), ['foo.js'], 'should match correctly.');
    test.deepEqual(grunt.file.match(['*.js', '*.css'], ['foo.js']), ['foo.js'], 'should match correctly.');
    test.deepEqual(grunt.file.match(['*.js', '*.css'], ['foo.js', 'bar.css']), ['foo.js', 'bar.css'], 'should match correctly.');
    test.done();
  },
  'no matches': function(test) {
    test.expect(2);
    test.deepEqual(grunt.file.match('*.js', 'foo.css'), [], 'should fail to match.');
    test.deepEqual(grunt.file.match('*.js', ['foo.css', 'bar.css']), [], 'should fail to match.');
    test.done();
  },
  'unique': function(test) {
    test.expect(2);
    test.deepEqual(grunt.file.match('*.js', ['foo.js', 'foo.js']), ['foo.js'], 'should return a uniqued set.');
    test.deepEqual(grunt.file.match(['*.js', '*.*'], ['foo.js', 'foo.js']), ['foo.js'], 'should return a uniqued set.');
    test.done();
  },
  'flatten': function(test) {
    test.expect(1);
    test.deepEqual(grunt.file.match([['*.js', '*.css'], ['*.*', '*.js']], ['foo.js', 'bar.css']), ['foo.js', 'bar.css'], 'should process nested pattern arrays correctly.');
    test.done();
  },
  'exclusion': function(test) {
    test.expect(5);
    test.deepEqual(grunt.file.match(['!*.js'], ['foo.js', 'bar.js']), [], 'solitary exclusion should match nothing');
    test.deepEqual(grunt.file.match(['*.js', '!*.js'], ['foo.js', 'bar.js']), [], 'exclusion should cancel match');
    test.deepEqual(grunt.file.match(['*.js', '!f*.js'], ['foo.js', 'bar.js', 'baz.js']), ['bar.js', 'baz.js'], 'partial exclusion should partially cancel match');
    test.deepEqual(grunt.file.match(['*.js', '!*.js', 'b*.js'], ['foo.js', 'bar.js', 'baz.js']), ['bar.js', 'baz.js'], 'inclusion / exclusion order matters');
    test.deepEqual(grunt.file.match(['*.js', '!f*.js', '*.js'], ['foo.js', 'bar.js', 'baz.js']), ['bar.js', 'baz.js', 'foo.js'], 'inclusion / exclusion order matters');
    test.done();
  },
  'options.matchBase': function(test) {
    test.expect(2);
    test.deepEqual(grunt.file.match({matchBase: true}, '*.js', ['foo.js', 'bar', 'baz/xyz.js']), ['foo.js', 'baz/xyz.js'], 'should matchBase (minimatch) when specified.');
    test.deepEqual(grunt.file.match('*.js', ['foo.js', 'bar', 'baz/xyz.js']), ['foo.js'], 'should not matchBase (minimatch) by default.');
    test.done();
  }
};

exports['file.isMatch'] = {
  'basic matching': function(test) {
    test.expect(6);
    test.ok(grunt.file.isMatch('*.js', 'foo.js'), 'should match correctly.');
    test.ok(grunt.file.isMatch('*.js', ['foo.js']), 'should match correctly.');
    test.ok(grunt.file.isMatch('*.js', ['foo.js', 'bar.css']), 'should match correctly.');
    test.ok(grunt.file.isMatch(['*.js', '*.css'], 'foo.js'), 'should match correctly.');
    test.ok(grunt.file.isMatch(['*.js', '*.css'], ['foo.js']), 'should match correctly.');
    test.ok(grunt.file.isMatch(['*.js', '*.css'], ['foo.js', 'bar.css']), 'should match correctly.');
    test.done();
  },
  'no matches': function(test) {
    test.expect(6);
    test.equal(grunt.file.isMatch('*.js', 'foo.css'), false, 'should fail to match.');
    test.equal(grunt.file.isMatch('*.js', ['foo.css', 'bar.css']), false, 'should fail to match.');
    test.equal(grunt.file.isMatch(null, 'foo.css'), false, 'should fail to match.');
    test.equal(grunt.file.isMatch('*.js', null), false, 'should fail to match.');
    test.equal(grunt.file.isMatch([], 'foo.css'), false, 'should fail to match.');
    test.equal(grunt.file.isMatch('*.js', []), false, 'should fail to match.');
    test.done();
  },
  'options.matchBase': function(test) {
    test.expect(2);
    test.ok(grunt.file.isMatch({matchBase: true}, '*.js', ['baz/xyz.js']), 'should matchBase (minimatch) when specified.');
    test.equal(grunt.file.isMatch('*.js', ['baz/xyz.js']), false, 'should not matchBase (minimatch) by default.');
    test.done();
  }
};

exports['file.expand*'] = {
  setUp: function(done) {
    this.cwd = process.cwd();
    process.chdir('test/fixtures/expand');
    done();
  },
  tearDown: function(done) {
    process.chdir(this.cwd);
    done();
  },
  'basic matching': function(test) {
    test.expect(8);
    test.deepEqual(grunt.file.expand('**/*.js'), ['js/bar.js', 'js/foo.js'], 'should match.');
    test.deepEqual(grunt.file.expand('**/*.js', '**/*.css'), ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'], 'should match.');
    test.deepEqual(grunt.file.expand(['**/*.js', '**/*.css']), ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'], 'should match.');
    test.deepEqual(grunt.file.expand('**d*/**'), [
      'deep',
      'deep/deep.txt',
      'deep/deeper',
      'deep/deeper/deeper.txt',
      'deep/deeper/deepest',
      'deep/deeper/deepest/deepest.txt'], 'should match files and directories.');
    test.deepEqual(grunt.file.expand({mark: true}, '**d*/**'), [
      'deep/',
      'deep/deep.txt',
      'deep/deeper/',
      'deep/deeper/deeper.txt',
      'deep/deeper/deepest/',
      'deep/deeper/deepest/deepest.txt'], 'the minimatch "mark" option ensures directories end in /.');
    test.deepEqual(grunt.file.expand('**d*/**/'), [
      'deep/',
      'deep/deeper/',
      'deep/deeper/deepest/'], 'should match directories, arbitrary / at the end appears in matches.');
    test.deepEqual(grunt.file.expand({mark: true}, '**d*/**/'), [
      'deep/',
      'deep/deeper/',
      'deep/deeper/deepest/'], 'should match directories, arbitrary / at the end appears in matches.');
    test.deepEqual(grunt.file.expand('*.xyz'), [], 'should fail to match.');
    test.done();
  },
  'filter': function(test) {
    test.expect(5);
    test.deepEqual(grunt.file.expand({filter: 'isFile'}, '**d*/**'), [
      'deep/deep.txt',
      'deep/deeper/deeper.txt',
      'deep/deeper/deepest/deepest.txt'
    ], 'should match files only.');
    test.deepEqual(grunt.file.expand({filter: 'isDirectory'}, '**d*/**'), [
      'deep',
      'deep/deeper',
      'deep/deeper/deepest'
    ], 'should match directories only.');
    test.deepEqual(grunt.file.expand({filter: function(filepath) { return (/deepest/).test(filepath); }}, '**'), [
      'deep/deeper/deepest',
      'deep/deeper/deepest/deepest.txt',
    ], 'should filter arbitrarily.');
    test.deepEqual(grunt.file.expand({filter: 'isFile'}, 'js', 'css'), [], 'should fail to match.');
    test.deepEqual(grunt.file.expand({filter: 'isDirectory'}, '**/*.js'), [], 'should fail to match.');
    test.done();
  },
  'unique': function(test) {
    test.expect(4);
    test.deepEqual(grunt.file.expand('**/*.js', 'js/*.js'), ['js/bar.js', 'js/foo.js'], 'file list should be uniqed.');
    test.deepEqual(grunt.file.expand('**/*.js', '**/*.css', 'js/*.js'), ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'], 'file list should be uniqed.');
    test.deepEqual(grunt.file.expand('js', 'js/'), ['js', 'js/'], 'mixed non-ending-/ and ending-/ dirs will not be uniqed by default.');
    test.deepEqual(grunt.file.expand({mark: true}, 'js', 'js/'), ['js/'], 'mixed non-ending-/ and ending-/ dirs will be uniqed when "mark" is specified.');
    test.done();
  },
  'file order': function(test) {
    test.expect(4);
    var actual = grunt.file.expand('**/*.{js,css}');
    var expected = ['css/baz.css', 'css/qux.css', 'js/bar.js', 'js/foo.js'];
    test.deepEqual(actual, expected, 'should select 4 files in this order, by default.');

    actual = grunt.file.expand('js/foo.js', 'js/bar.js', '**/*.{js,css}');
    expected = ['js/foo.js', 'js/bar.js', 'css/baz.css', 'css/qux.css'];
    test.deepEqual(actual, expected, 'specifically-specified-up-front file order should be maintained.');

    actual = grunt.file.expand('js/bar.js', 'js/foo.js', '**/*.{js,css}');
    expected = ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'];
    test.deepEqual(actual, expected, 'specifically-specified-up-front file order should be maintained.');

    actual = grunt.file.expand('js/foo.js', '**/*.{js,css}', '!js/bar.js', 'js/bar.js');
    expected = ['js/foo.js', 'css/baz.css', 'css/qux.css', 'js/bar.js'];
    test.deepEqual(actual, expected, 'if a file is excluded and then re-added, it should be added at the end.');
    test.done();
  },
  'flatten': function(test) {
    test.expect(1);
    test.deepEqual(grunt.file.expand([['**/*.js'], ['**/*.css', 'js/*.js']]), ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'], 'should match.');
    test.done();
  },
  'exclusion': function(test) {
    test.expect(8);
    test.deepEqual(grunt.file.expand(['!js/*.js']), [], 'solitary exclusion should match nothing');
    test.deepEqual(grunt.file.expand(['js/bar.js', '!js/bar.js']), [], 'exclusion should cancel match');
    test.deepEqual(grunt.file.expand(['**/*.js', '!js/foo.js']), ['js/bar.js'], 'should omit single file from matched set');
    test.deepEqual(grunt.file.expand(['!js/foo.js', '**/*.js']), ['js/bar.js', 'js/foo.js'], 'inclusion / exclusion order matters');
    test.deepEqual(grunt.file.expand(['**/*.js', '**/*.css', '!js/bar.js', '!css/baz.css']), ['js/foo.js', 'css/qux.css'], 'multiple exclusions should be removed from the set');
    test.deepEqual(grunt.file.expand(['**/*.js', '**/*.css', '!**/*.css']), ['js/bar.js', 'js/foo.js'], 'excluded wildcards should be removed from the matched set');
    test.deepEqual(grunt.file.expand(['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css', '!**/b*.*']), ['js/foo.js', 'css/qux.css'], 'different pattern for exclusion should still work');
    test.deepEqual(grunt.file.expand(['js/bar.js', '!**/b*.*', 'js/foo.js', 'css/baz.css', 'css/qux.css']), ['js/foo.js', 'css/baz.css', 'css/qux.css'], 'inclusion / exclusion order matters');
    test.done();
  },
  'options.matchBase': function(test) {
    test.expect(4);
    var opts = {matchBase: true};
    test.deepEqual(grunt.file.expand('*.js'), [], 'should not matchBase (minimatch) by default.');
    test.deepEqual(grunt.file.expand(opts, '*.js'), ['js/bar.js', 'js/foo.js'], 'options should be passed through to minimatch.');
    test.deepEqual(grunt.file.expand(opts, '*.js', '*.css'), ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'], 'should match.');
    test.deepEqual(grunt.file.expand(opts, ['*.js', '*.css']), ['js/bar.js', 'js/foo.js', 'css/baz.css', 'css/qux.css'], 'should match.');
    test.done();
  },
  'options.cwd': function(test) {
    test.expect(4);
    var cwd = path.resolve(process.cwd(), '..');
    test.deepEqual(grunt.file.expand({cwd: cwd}, ['expand/js', 'expand/js/*']), ['expand/js', 'expand/js/bar.js', 'expand/js/foo.js'], 'should match.');
    test.deepEqual(grunt.file.expand({cwd: cwd, filter: 'isFile'}, ['expand/js', 'expand/js/*']), ['expand/js/bar.js', 'expand/js/foo.js'], 'should match.');
    test.deepEqual(grunt.file.expand({cwd: cwd, filter: 'isDirectory'}, ['expand/js', 'expand/js/*']), ['expand/js'], 'should match.');
    test.deepEqual(grunt.file.expand({cwd: cwd, filter: 'isFile'}, ['expand/js', 'expand/js/*', '!**/b*.js']), ['expand/js/foo.js'], 'should negate properly.');
    test.done();
  },
  'options.nonull': function(test) {
    test.expect(2);
    var opts = {nonull: true};
    test.deepEqual(grunt.file.expand(opts, ['js/a*', 'js/b*', 'js/c*']), ['js/a*', 'js/bar.js', 'js/c*'], 'non-matching patterns should be returned in result set.');
    test.deepEqual(grunt.file.expand(opts, ['js/foo.js', 'js/bar.js', 'js/baz.js']), ['js/foo.js', 'js/bar.js', 'js/baz.js'], 'non-matching filenames should be returned in result set.');
    test.done();
  },
};

exports['file.expandMapping'] = {
  setUp: function(done) {
    this.cwd = process.cwd();
    process.chdir('test/fixtures');
    done();
  },
  tearDown: function(done) {
    process.chdir(this.cwd);
    done();
  },
  'basic matching': function(test) {
    test.expect(2);

    var actual = grunt.file.expandMapping(['expand/**/*.txt'], 'dest');
    var expected = [
      {dest: 'dest/expand/deep/deep.txt', src: ['expand/deep/deep.txt']},
      {dest: 'dest/expand/deep/deeper/deeper.txt', src: ['expand/deep/deeper/deeper.txt']},
      {dest: 'dest/expand/deep/deeper/deepest/deepest.txt', src: ['expand/deep/deeper/deepest/deepest.txt']},
    ];
    test.deepEqual(actual, expected, 'basic src-dest options');

    actual = grunt.file.expandMapping(['expand/**/*.txt'], 'dest/');
    test.deepEqual(actual, expected, 'destBase should behave the same both with or without trailing slash');

    test.done();
  },
  'flatten': function(test) {
    test.expect(1);
    var actual = grunt.file.expandMapping(['expand/**/*.txt'], 'dest', {flatten: true});
    var expected = [
      {dest: 'dest/deep.txt', src: ['expand/deep/deep.txt']},
      {dest: 'dest/deeper.txt', src: ['expand/deep/deeper/deeper.txt']},
      {dest: 'dest/deepest.txt', src: ['expand/deep/deeper/deepest/deepest.txt']},
    ];
    test.deepEqual(actual, expected, 'dest paths should be flattened pre-destBase+destPath join');
    test.done();
  },
  'ext': function(test) {
    test.expect(3);
    var actual, expected;
    actual = grunt.file.expandMapping(['expand/**/*.txt'], 'dest', {ext: '.foo'});
    expected = [
      {dest: 'dest/expand/deep/deep.foo', src: ['expand/deep/deep.txt']},
      {dest: 'dest/expand/deep/deeper/deeper.foo', src: ['expand/deep/deeper/deeper.txt']},
      {dest: 'dest/expand/deep/deeper/deepest/deepest.foo', src: ['expand/deep/deeper/deepest/deepest.txt']},
    ];
    test.deepEqual(actual, expected, 'specified extension should be added');
    actual = grunt.file.expandMapping(['expand-mapping-ext/**/file*'], 'dest', {ext: '.foo'});
    expected = [
      {dest: 'dest/expand-mapping-ext/dir.ectory/file-no-extension.foo', src: ['expand-mapping-ext/dir.ectory/file-no-extension']},
      {dest: 'dest/expand-mapping-ext/dir.ectory/sub.dir.ectory/file.foo', src: ['expand-mapping-ext/dir.ectory/sub.dir.ectory/file.ext.ension']},
      {dest: 'dest/expand-mapping-ext/file.foo', src: ['expand-mapping-ext/file.ext.ension']},
    ];
    test.deepEqual(actual, expected, 'specified extension should be added');
    actual = grunt.file.expandMapping(['expand/**/*.txt'], 'dest', {ext: ''});
    expected = [
      {dest: 'dest/expand/deep/deep', src: ['expand/deep/deep.txt']},
      {dest: 'dest/expand/deep/deeper/deeper', src: ['expand/deep/deeper/deeper.txt']},
      {dest: 'dest/expand/deep/deeper/deepest/deepest', src: ['expand/deep/deeper/deepest/deepest.txt']},
    ];
    test.deepEqual(actual, expected, 'empty string extension should be added');
    test.done();
  },
  'extDot': function(test) {
    test.expect(2);
    var actual, expected;

    actual = grunt.file.expandMapping(['expand-mapping-ext/**/file*'], 'dest', {ext: '.foo', extDot: 'first'});
    expected = [
      {dest: 'dest/expand-mapping-ext/dir.ectory/file-no-extension.foo', src: ['expand-mapping-ext/dir.ectory/file-no-extension']},
      {dest: 'dest/expand-mapping-ext/dir.ectory/sub.dir.ectory/file.foo', src: ['expand-mapping-ext/dir.ectory/sub.dir.ectory/file.ext.ension']},
      {dest: 'dest/expand-mapping-ext/file.foo', src: ['expand-mapping-ext/file.ext.ension']},
    ];
    test.deepEqual(actual, expected, 'extDot of "first" should replace everything after the first dot in the filename.');

    actual = grunt.file.expandMapping(['expand-mapping-ext/**/file*'], 'dest', {ext: '.foo', extDot: 'last'});
    expected = [
      {dest: 'dest/expand-mapping-ext/dir.ectory/file-no-extension.foo', src: ['expand-mapping-ext/dir.ectory/file-no-extension']},
      {dest: 'dest/expand-mapping-ext/dir.ectory/sub.dir.ectory/file.ext.foo', src: ['expand-mapping-ext/dir.ectory/sub.dir.ectory/file.ext.ension']},
      {dest: 'dest/expand-mapping-ext/file.ext.foo', src: ['expand-mapping-ext/file.ext.ension']},
    ];
    test.deepEqual(actual, expected, 'extDot of "last" should replace everything after the last dot in the filename.');

    test.done();
  },
  'cwd': function(test) {
    test.expect(1);
    var actual = grunt.file.expandMapping(['**/*.txt'], 'dest', {cwd: 'expand'});
    var expected = [
      {dest: 'dest/deep/deep.txt', src: ['expand/deep/deep.txt']},
      {dest: 'dest/deep/deeper/deeper.txt', src: ['expand/deep/deeper/deeper.txt']},
      {dest: 'dest/deep/deeper/deepest/deepest.txt', src: ['expand/deep/deeper/deepest/deepest.txt']},
    ];
    test.deepEqual(actual, expected, 'cwd should be stripped from front of destPath, pre-destBase+destPath join');
    test.done();
  },
  'rename': function(test) {
    test.expect(1);
    var actual = grunt.file.expandMapping(['**/*.txt'], 'dest', {
      cwd: 'expand',
      flatten: true,
      rename: function(destBase, destPath, options) {
        return path.join(destBase, options.cwd, 'o-m-g', destPath);
      }
    });
    var expected = [
      {dest: 'dest/expand/o-m-g/deep.txt', src: ['expand/deep/deep.txt']},
      {dest: 'dest/expand/o-m-g/deeper.txt', src: ['expand/deep/deeper/deeper.txt']},
      {dest: 'dest/expand/o-m-g/deepest.txt', src: ['expand/deep/deeper/deepest/deepest.txt']},
    ];
    test.deepEqual(actual, expected, 'custom rename function should be used to build dest, post-flatten');
    test.done();
  },
  'rename to same dest': function(test) {
    test.expect(1);
    var actual = grunt.file.expandMapping(['**/*'], 'dest', {
      filter: 'isFile',
      cwd: 'expand',
      flatten: true,
      nosort: true,
      rename: function(destBase, destPath) {
        return path.join(destBase, 'all' + path.extname(destPath));
      }
    });
    var expected = [
      {dest: 'dest/all.md', src: ['expand/README.md']},
      {dest: 'dest/all.css', src: ['expand/css/baz.css', 'expand/css/qux.css']},
      {dest: 'dest/all.txt', src: ['expand/deep/deep.txt', 'expand/deep/deeper/deeper.txt', 'expand/deep/deeper/deepest/deepest.txt']},
      {dest: 'dest/all.js', src: ['expand/js/bar.js', 'expand/js/foo.js']},
    ];
    test.deepEqual(actual, expected, 'if dest is same for multiple src, create an array of src');
    test.done();
  },
};

// Compare two buffers. Returns true if they are equivalent.
var compareBuffers = function(buf1, buf2) {
  if (!Buffer.isBuffer(buf1) || !Buffer.isBuffer(buf2)) { return false; }
  if (buf1.length !== buf2.length) { return false; }
  for (var i = 0; i < buf2.length; i++) {
    if (buf1[i] !== buf2[i]) { return false; }
  }
  return true;
};

// Compare two files. Returns true if they are equivalent.
var compareFiles = function(filepath1, filepath2) {
  return compareBuffers(fs.readFileSync(filepath1), fs.readFileSync(filepath2));
};

exports.file = {
  setUp: function(done) {
    this.defaultEncoding = grunt.file.defaultEncoding;
    grunt.file.defaultEncoding = 'utf8';
    this.string = 'Ação é isso aí\n';
    this.object = {foo: 'Ação é isso aí', bar: ['ømg', 'pønies']};
    this.writeOption = grunt.option('write');

    // Testing that warnings were displayed.
    this.oldFailWarnFn = grunt.fail.warn;
    this.oldLogWarnFn = grunt.log.warn;
    this.resetWarnCount = function() {
      this.warnCount = 0;
    }.bind(this);
    grunt.fail.warn = grunt.log.warn = function() {
      this.warnCount += 1;
    }.bind(this);

    done();
  },
  tearDown: function(done) {
    grunt.file.defaultEncoding = this.defaultEncoding;
    grunt.option('write', this.writeOption);

    grunt.fail.warn = this.oldFailWarnFn;
    grunt.log.warn = this.oldLogWarnFn;

    done();
  },
  'read': function(test) {
    test.expect(6);
    test.strictEqual(grunt.file.read('test/fixtures/utf8.txt'), this.string, 'file should be read as utf8 by default.');
    test.strictEqual(grunt.file.read('test/fixtures/iso-8859-1.txt', {encoding: 'iso-8859-1'}), this.string, 'file should be read using the specified encoding.');
    test.ok(compareBuffers(grunt.file.read('test/fixtures/octocat.png', {encoding: null}), fs.readFileSync('test/fixtures/octocat.png')), 'file should be read as a buffer if encoding is specified as null.');

    test.strictEqual(grunt.file.read('test/fixtures/BOM.txt'), 'foo', 'file should have BOM stripped.');
    grunt.file.preserveBOM = true;
    test.strictEqual(grunt.file.read('test/fixtures/BOM.txt'), '\ufeff' + 'foo', 'file should have BOM preserved.');
    grunt.file.preserveBOM = false;

    grunt.file.defaultEncoding = 'iso-8859-1';
    test.strictEqual(grunt.file.read('test/fixtures/iso-8859-1.txt'), this.string, 'changing the default encoding should work.');
    test.done();
  },
  'readJSON': function(test) {
    test.expect(3);
    var obj;
    obj = grunt.file.readJSON('test/fixtures/utf8.json');
    test.deepEqual(obj, this.object, 'file should be read as utf8 by default and parsed correctly.');

    obj = grunt.file.readJSON('test/fixtures/iso-8859-1.json', {encoding: 'iso-8859-1'});
    test.deepEqual(obj, this.object, 'file should be read using the specified encoding.');

    grunt.file.defaultEncoding = 'iso-8859-1';
    obj = grunt.file.readJSON('test/fixtures/iso-8859-1.json');
    test.deepEqual(obj, this.object, 'changing the default encoding should work.');
    test.done();
  },
  'readYAML': function(test) {
    test.expect(4);
    var obj;
    obj = grunt.file.readYAML('test/fixtures/utf8.yaml');
    test.deepEqual(obj, this.object, 'file should be read as utf8 by default and parsed correctly.');

    obj = grunt.file.readYAML('test/fixtures/iso-8859-1.yaml', {encoding: 'iso-8859-1'});
    test.deepEqual(obj, this.object, 'file should be read using the specified encoding.');

    test.throws(function() {
      obj = grunt.file.readYAML('test/fixtures/error.yaml');
    }, function(err) {
      return err.message.indexOf('undefined') === -1;
    }, 'error thrown should not contain undefined.');

    grunt.file.defaultEncoding = 'iso-8859-1';
    obj = grunt.file.readYAML('test/fixtures/iso-8859-1.yaml');
    test.deepEqual(obj, this.object, 'changing the default encoding should work.');
    test.done();
  },
  'write': function(test) {
    test.expect(6);
    var tmpfile;
    tmpfile = new Tempfile();
    grunt.file.write(tmpfile.path, this.string);
    test.strictEqual(fs.readFileSync(tmpfile.path, 'utf8'), this.string, 'file should be written as utf8 by default.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    grunt.file.write(tmpfile.path, this.string, {encoding: 'iso-8859-1'});
    test.strictEqual(grunt.file.read(tmpfile.path, {encoding: 'iso-8859-1'}), this.string, 'file should be written using the specified encoding.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    tmpfile.unlinkSync();
    grunt.file.write(tmpfile.path, this.string, {mode: parseInt('0444', 8)});
    test.strictEqual(fs.statSync(tmpfile.path).mode & parseInt('0222', 8), 0, 'file should be read only.');
    fs.chmodSync(tmpfile.path, parseInt('0666', 8));
    tmpfile.unlinkSync();

    grunt.file.defaultEncoding = 'iso-8859-1';
    tmpfile = new Tempfile();
    grunt.file.write(tmpfile.path, this.string);
    grunt.file.defaultEncoding = 'utf8';
    test.strictEqual(grunt.file.read(tmpfile.path, {encoding: 'iso-8859-1'}), this.string, 'changing the default encoding should work.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    var octocat = fs.readFileSync('test/fixtures/octocat.png');
    grunt.file.write(tmpfile.path, octocat);
    test.ok(compareBuffers(fs.readFileSync(tmpfile.path), octocat), 'buffers should always be written as-specified, with no attempt at re-encoding.');
    tmpfile.unlinkSync();

    grunt.option('write', false);
    var filepath = path.join(tmpdir.path, 'should-not-exist.txt');
    grunt.file.write(filepath, 'test');
    test.equal(grunt.file.exists(filepath), false, 'file should NOT be created if --no-write was specified.');
    test.done();
  },
  'copy': function(test) {
    test.expect(4);
    var tmpfile;
    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/utf8.txt', tmpfile.path);
    test.ok(compareFiles(tmpfile.path, 'test/fixtures/utf8.txt'), 'files should just be copied as encoding-agnostic by default.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/iso-8859-1.txt', tmpfile.path);
    test.ok(compareFiles(tmpfile.path, 'test/fixtures/iso-8859-1.txt'), 'files should just be copied as encoding-agnostic by default.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/octocat.png', tmpfile.path);
    test.ok(compareFiles(tmpfile.path, 'test/fixtures/octocat.png'), 'files should just be copied as encoding-agnostic by default.');
    tmpfile.unlinkSync();

    grunt.option('write', false);
    var filepath = path.join(tmpdir.path, 'should-not-exist.txt');
    grunt.file.copy('test/fixtures/utf8.txt', filepath);
    test.equal(grunt.file.exists(filepath), false, 'file should NOT be created if --no-write was specified.');
    test.done();
  },
  'copy and process': function(test) {
    test.expect(14);
    var tmpfile;
    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/utf8.txt', tmpfile.path, {
      process: function(src, srcpath, destpath) {
        test.equal(srcpath, 'test/fixtures/utf8.txt', 'srcpath should be passed in, as-specified.');
        test.equal(destpath, tmpfile.path, 'destpath should be passed in, as-specified.');
        test.equal(Buffer.isBuffer(src), false, 'when no encoding is specified, use default encoding and process src as a string');
        test.equal(typeof src, 'string', 'when no encoding is specified, use default encoding and process src as a string');
        return 'føø' + src + 'bår';
      }
    });
    test.equal(grunt.file.read(tmpfile.path), 'føø' + this.string + 'bår', 'file should be saved as properly encoded processed string.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/iso-8859-1.txt', tmpfile.path, {
      encoding: 'iso-8859-1',
      process: function(src) {
        test.equal(Buffer.isBuffer(src), false, 'use specified encoding and process src as a string');
        test.equal(typeof src, 'string', 'use specified encoding and process src as a string');
        return 'føø' + src + 'bår';
      }
    });
    test.equal(grunt.file.read(tmpfile.path, {encoding: 'iso-8859-1'}), 'føø' + this.string + 'bår', 'file should be saved as properly encoded processed string.');
    tmpfile.unlinkSync();

    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/utf8.txt', tmpfile.path, {
      encoding: null,
      process: function(src) {
        test.ok(Buffer.isBuffer(src), 'when encoding is specified as null, process src as a buffer');
        return Buffer.from('føø' + src.toString() + 'bår');
      }
    });
    test.equal(grunt.file.read(tmpfile.path), 'føø' + this.string + 'bår', 'file should be saved as the buffer returned by process.');
    tmpfile.unlinkSync();

    grunt.file.defaultEncoding = 'iso-8859-1';
    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/iso-8859-1.txt', tmpfile.path, {
      process: function(src) {
        test.equal(Buffer.isBuffer(src), false, 'use non-utf8 default encoding and process src as a string');
        test.equal(typeof src, 'string', 'use non-utf8 default encoding and process src as a string');
        return 'føø' + src + 'bår';
      }
    });
    test.equal(grunt.file.read(tmpfile.path), 'føø' + this.string + 'bår', 'file should be saved as properly encoded processed string.');
    tmpfile.unlinkSync();

    var filepath = path.join(tmpdir.path, 'should-not-exist.txt');
    grunt.file.copy('test/fixtures/iso-8859-1.txt', filepath, {
      process: function() {
        return false;
      }
    });
    test.equal(grunt.file.exists(filepath), false, 'file should NOT be created if process returns false.');
    test.done();
  },
  'copy and process, noprocess': function(test) {
    test.expect(4);
    var tmpfile;
    tmpfile = new Tempfile();
    grunt.file.copy('test/fixtures/utf8.txt', tmpfile.path, {
      noProcess: true,
      process: function(src) {
        return 'føø' + src + 'bår';
      }
    });
    test.equal(grunt.file.read(tmpfile.path), this.string, 'file should not have been processed.');
    tmpfile.unlinkSync();

    ['process', 'noprocess', 'othernoprocess'].forEach(function(filename) {
      var filepath = path.join(tmpdir.path, filename);
      grunt.file.copy('test/fixtures/utf8.txt', filepath);
      var tmpfile = new Tempfile();
      grunt.file.copy(filepath, tmpfile.path, {
        noProcess: ['**/*no*'],
        process: function(src) {
          return 'føø' + src + 'bår';
        }
      });
      if (filename === 'process') {
        test.equal(grunt.file.read(tmpfile.path), 'føø' + this.string + 'bår', 'file should have been processed.');
      } else {
        test.equal(grunt.file.read(tmpfile.path), this.string, 'file should not have been processed.');
      }
      tmpfile.unlinkSync();
    }, this);

    test.done();
  },
  'copy directory recursively': function(test) {
    test.expect(34);
    var copyroot1 = path.join(tmpdir.path, 'copy-dir-1');
    var copyroot2 = path.join(tmpdir.path, 'copy-dir-2');
    grunt.file.copy('test/fixtures/expand/', copyroot1);
    grunt.file.recurse('test/fixtures/expand/', function(srcpath, rootdir, subdir, filename) {
      var destpath = path.join(copyroot1, subdir || '', filename);
      test.ok(grunt.file.isFile(srcpath), 'file should have been copied.');
      test.equal(grunt.file.read(srcpath), grunt.file.read(destpath), 'file contents should be the same.');
    });
    grunt.file.mkdir(path.join(copyroot1, 'empty'));
    grunt.file.mkdir(path.join(copyroot1, 'deep/deeper/empty'));
    grunt.file.copy(copyroot1, copyroot2, {
      process: function(contents) {
        return '<' + contents + '>';
      },
    });
    test.ok(grunt.file.isDir(path.join(copyroot2, 'empty')), 'empty directory should have been created.');
    test.ok(grunt.file.isDir(path.join(copyroot2, 'deep/deeper/empty')), 'empty directory should have been created.');
    grunt.file.recurse('test/fixtures/expand/', function(srcpath, rootdir, subdir, filename) {
      var destpath = path.join(copyroot2, subdir || '', filename);
      test.ok(grunt.file.isFile(srcpath), 'file should have been copied.');
      test.equal('<' + grunt.file.read(srcpath) + '>', grunt.file.read(destpath), 'file contents should be processed correctly.');
    });
    test.done();
  },
  'delete': function(test) {
    test.expect(2);
    var oldBase = process.cwd();
    var cwd = path.resolve(tmpdir.path, 'delete', 'folder');
    grunt.file.mkdir(cwd);
    grunt.file.setBase(tmpdir.path);

    grunt.file.write(path.join(cwd, 'test.js'), 'var test;');
    test.ok(grunt.file.delete(cwd), 'should return true after deleting file.');
    test.equal(grunt.file.exists(cwd), false, 'file should have been deleted.');
    grunt.file.setBase(oldBase);
    test.done();
  },
  'delete nonexistent file': function(test) {
    test.expect(2);
    this.resetWarnCount();
    test.ok(!grunt.file.delete('nonexistent'), 'should return false if file does not exist.');
    test.ok(this.warnCount, 'should issue a warning when deleting non-existent file');
    test.done();
  },
  'delete outside working directory': function(test) {
    test.expect(4);
    var oldBase = process.cwd();
    var cwd = path.resolve(tmpdir.path, 'delete', 'folder');
    var outsidecwd = path.resolve(tmpdir.path, 'delete', 'outsidecwd');
    grunt.file.mkdir(cwd);
    grunt.file.mkdir(outsidecwd);
    grunt.file.setBase(cwd);

    grunt.file.write(path.join(outsidecwd, 'test.js'), 'var test;');

    this.resetWarnCount();
    test.equal(grunt.file.delete(path.join(outsidecwd, 'test.js')), false, 'should not delete anything outside the cwd.');
    test.ok(this.warnCount, 'should issue a warning when deleting outside working directory');

    test.ok(grunt.file.delete(path.join(outsidecwd), {force: true}), 'should delete outside cwd when using the --force.');
    test.equal(grunt.file.exists(outsidecwd), false, 'file outside cwd should have been deleted when using the --force.');

    grunt.file.setBase(oldBase);
    test.done();
  },
  'dont delete current working directory': function(test) {
    test.expect(3);
    var oldBase = process.cwd();
    var cwd = path.resolve(tmpdir.path, 'dontdelete', 'folder');
    grunt.file.mkdir(cwd);
    grunt.file.setBase(cwd);

    this.resetWarnCount();
    test.equal(grunt.file.delete(cwd), false, 'should not delete the cwd.');
    test.ok(this.warnCount, 'should issue a warning when trying to delete cwd');

    test.ok(grunt.file.exists(cwd), 'the cwd should exist.');

    grunt.file.setBase(oldBase);
    test.done();
  },
  'dont actually delete with no-write option on': function(test) {
    test.expect(2);
    grunt.option('write', false);

    var oldBase = process.cwd();
    var cwd = path.resolve(tmpdir.path, 'dontdelete', 'folder');
    grunt.file.mkdir(cwd);
    grunt.file.setBase(tmpdir.path);

    grunt.file.write(path.join(cwd, 'test.js'), 'var test;');
    test.ok(grunt.file.delete(cwd), 'should return true after not actually deleting file.');
    test.equal(grunt.file.exists(cwd), true, 'file should NOT be deleted if --no-write was specified.');
    grunt.file.setBase(oldBase);

    test.done();
  },
  'mkdir': function(test) {
    test.expect(5);
    test.doesNotThrow(function() {
      grunt.file.mkdir(tmpdir.path);
    }, 'Should not explode if the directory already exists.');
    test.ok(fs.existsSync(tmpdir.path), 'path should still exist.');

    test.doesNotThrow(function() {
      grunt.file.mkdir(path.join(tmpdir.path, 'aa/bb/cc'));
    }, 'Should also not explode, otherwise.');
    test.ok(path.join(tmpdir.path, 'aa/bb/cc'), 'path should have been created.');

    fs.writeFileSync(path.join(tmpdir.path, 'aa/bb/xx'), 'test');
    test.throws(function() {
      grunt.file.mkdir(path.join(tmpdir.path, 'aa/bb/xx/yy'));
    }, 'Should throw if a path cannot be created (ENOTDIR).');

    test.done();
  },
  'recurse': function(test) {
    test.expect(1);
    var rootdir = 'test/fixtures/expand';
    var expected = {};
    expected[rootdir + '/css/baz.css'] = [rootdir, 'css', 'baz.css'];
    expected[rootdir + '/css/qux.css'] = [rootdir, 'css', 'qux.css'];
    expected[rootdir + '/deep/deep.txt'] = [rootdir, 'deep', 'deep.txt'];
    expected[rootdir + '/deep/deeper/deeper.txt'] = [rootdir, 'deep/deeper', 'deeper.txt'];
    expected[rootdir + '/deep/deeper/deepest/deepest.txt'] = [rootdir, 'deep/deeper/deepest', 'deepest.txt'];
    expected[rootdir + '/js/bar.js'] = [rootdir, 'js', 'bar.js'];
    expected[rootdir + '/js/foo.js'] = [rootdir, 'js', 'foo.js'];
    expected[rootdir + '/README.md'] = [rootdir, undefined, 'README.md'];

    var actual = {};
    grunt.file.recurse(rootdir, function(abspath, rootdir, subdir, filename) {
      actual[abspath] = [rootdir, subdir, filename];
    });

    test.deepEqual(actual, expected, 'paths and arguments should match.');
    test.done();
  },
  'exists': function(test) {
    test.expect(6);
    test.ok(grunt.file.exists('test/fixtures/octocat.png'), 'files exist.');
    test.ok(grunt.file.exists('test', 'fixtures', 'octocat.png'), 'should work for paths in parts.');
    test.ok(grunt.file.exists('test/fixtures'), 'directories exist.');
    test.ok(grunt.file.exists(path.join(tmpdir.path, 'octocat.png')), 'file links exist.');
    test.ok(grunt.file.exists(path.join(tmpdir.path, 'expand')), 'directory links exist.');
    test.equal(grunt.file.exists('test/fixtures/does/not/exist'), false, 'nonexistent files do not exist.');
    test.done();
  },
  'isLink': function(test) {
    test.expect(8);
    test.equals(grunt.file.isLink('test/fixtures/octocat.png'), false, 'files are not links.');
    test.equals(grunt.file.isLink('test/fixtures'), false, 'directories are not links.');
    test.ok(grunt.file.isLink(path.join(tmpdir.path, 'octocat.png')), 'file links are links.');
    test.ok(grunt.file.isLink(path.join(tmpdir.path, 'expand')), 'directory links are links.');
    grunt.file.mkdir(path.join(tmpdir.path, 'relative-links'));
    fs.symlinkSync('test/fixtures/octocat.png', path.join(tmpdir.path, 'relative-links/octocat.png'), 'file');
    fs.symlinkSync('test/fixtures/expand', path.join(tmpdir.path, 'relative-links/expand'), 'file');
    test.ok(grunt.file.isLink(path.join(tmpdir.path, 'relative-links/octocat.png')), 'relative file links are links.');
    test.ok(grunt.file.isLink(path.join(tmpdir.path, 'relative-links/expand')), 'relative directory links are links.');
    test.ok(grunt.file.isLink(tmpdir.path, 'octocat.png'), 'should work for paths in parts.');
    test.equals(grunt.file.isLink('test/fixtures/does/not/exist'), false, 'nonexistent files are not links.');
    test.done();
  },
  'isDir': function(test) {
    test.expect(6);
    test.equals(grunt.file.isDir('test/fixtures/octocat.png'), false, 'files are not directories.');
    test.ok(grunt.file.isDir('test/fixtures'), 'directories are directories.');
    test.ok(grunt.file.isDir('test', 'fixtures'), 'should work for paths in parts.');
    test.equals(grunt.file.isDir(path.join(tmpdir.path, 'octocat.png')), false, 'file links are not directories.');
    test.ok(grunt.file.isDir(path.join(tmpdir.path, 'expand')), 'directory links are directories.');
    test.equals(grunt.file.isDir('test/fixtures/does/not/exist'), false, 'nonexistent files are not directories.');
    test.done();
  },
  'isFile': function(test) {
    test.expect(6);
    test.ok(grunt.file.isFile('test/fixtures/octocat.png'), 'files are files.');
    test.ok(grunt.file.isFile('test', 'fixtures', 'octocat.png'), 'should work for paths in parts.');
    test.equals(grunt.file.isFile('test/fixtures'), false, 'directories are not files.');
    test.ok(grunt.file.isFile(path.join(tmpdir.path, 'octocat.png')), 'file links are files.');
    test.equals(grunt.file.isFile(path.join(tmpdir.path, 'expand')), false, 'directory links are not files.');
    test.equals(grunt.file.isFile('test/fixtures/does/not/exist'), false, 'nonexistent files are not files.');
    test.done();
  },
  'isPathAbsolute': function(test) {
    test.expect(6);
    test.ok(grunt.file.isPathAbsolute(path.resolve('/foo')), 'should return true');
    test.ok(grunt.file.isPathAbsolute(path.resolve('/foo') + path.sep), 'should return true');
    test.equal(grunt.file.isPathAbsolute('foo'), false, 'should return false');
    test.ok(grunt.file.isPathAbsolute(path.resolve('test/fixtures/a.js')), 'should return true');
    test.equal(grunt.file.isPathAbsolute('test/fixtures/a.js'), false, 'should return false');
    if (win32) {
      test.equal(grunt.file.isPathAbsolute('C:/Users/'), true, 'should return true');
    } else {
      test.equal(grunt.file.isPathAbsolute('/'), true, 'should return true');
    }
    test.done();
  },
  'arePathsEquivalent': function(test) {
    test.expect(5);
    test.ok(grunt.file.arePathsEquivalent('/foo'), 'should return true');
    test.ok(grunt.file.arePathsEquivalent('/foo', '/foo/', '/foo/../foo/'), 'should return true');
    test.ok(grunt.file.arePathsEquivalent(process.cwd(), '.', './', 'test/..'), 'should return true');
    test.equal(grunt.file.arePathsEquivalent(process.cwd(), '..'), false, 'should return false');
    test.equal(grunt.file.arePathsEquivalent('.', '..'), false, 'should return false');
    test.done();
  },
  'doesPathContain': function(test) {
    test.expect(6);
    test.ok(grunt.file.doesPathContain('/foo', '/foo/bar'), 'should return true');
    test.ok(grunt.file.doesPathContain('/foo/', '/foo/bar/baz', '/foo/bar', '/foo/whatever'), 'should return true');
    test.equal(grunt.file.doesPathContain('/foo', '/foo'), false, 'should return false');
    test.equal(grunt.file.doesPathContain('/foo/xyz', '/foo/xyz/123', '/foo/bar/baz'), false, 'should return false');
    test.equal(grunt.file.doesPathContain('/foo/xyz', '/foo'), false, 'should return false');
    test.ok(grunt.file.doesPathContain(process.cwd(), 'test', 'test/fixtures', 'lib'), 'should return true');
    test.done();
  },
  'isPathCwd': function(test) {
    test.expect(8);
    test.ok(grunt.file.isPathCwd(process.cwd()), 'cwd is cwd');
    test.ok(grunt.file.isPathCwd('.'), 'cwd is cwd');
    test.equal(grunt.file.isPathCwd('test'), false, 'subdirectory is not cwd');
    test.equal(grunt.file.isPathCwd(path.resolve('test')), false, 'subdirectory is not cwd');
    test.equal(grunt.file.isPathCwd('..'), false, 'parent is not cwd');
    test.equal(grunt.file.isPathCwd(path.resolve('..')), false, 'parent is not cwd');
    test.equal(grunt.file.isPathCwd('/'), false, 'root is not cwd (I hope)');
    test.equal(grunt.file.isPathCwd('nonexistent'), false, 'nonexistent path is not cwd');
    test.done();
  },
  'isPathInCwd': function(test) {
    test.expect(8);
    test.equal(grunt.file.isPathInCwd(process.cwd()), false, 'cwd is not IN cwd');
    test.equal(grunt.file.isPathInCwd('.'), false, 'cwd is not IN cwd');
    test.ok(grunt.file.isPathInCwd('test'), 'subdirectory is in cwd');
    test.ok(grunt.file.isPathInCwd(path.resolve('test')), 'subdirectory is in cwd');
    test.equal(grunt.file.isPathInCwd('..'), false, 'parent is not in cwd');
    test.equal(grunt.file.isPathInCwd(path.resolve('..')), false, 'parent is not in cwd');
    test.equal(grunt.file.isPathInCwd('/'), false, 'root is not in cwd (I hope)');
    test.equal(grunt.file.isPathInCwd('nonexistent'), false, 'nonexistent path is not in cwd');
    test.done();
  },
  'cwdUnderSymlink': {
    setUp: function(done) {
      this.cwd = process.cwd();
      process.chdir(path.join(tmpdir.path, 'expand'));
      done();
    },
    tearDown: function(done) {
      process.chdir(this.cwd);
      done();
    },
    'isPathCwd': function(test) {
      test.expect(2);
      test.ok(grunt.file.isPathCwd(process.cwd()), 'cwd is cwd');
      test.ok(grunt.file.isPathCwd('.'), 'cwd is cwd');
      test.done();
    },
    'isPathInCwd': function(test) {
      test.expect(2);
      test.ok(grunt.file.isPathInCwd('deep'), 'subdirectory is in cwd');
      test.ok(grunt.file.isPathInCwd(path.resolve('deep')), 'subdirectory is in cwd');
      test.done();
    },
  }
};
```
