# Patch-overlapping JavaScript context before the fix

## `lib/ejs.js` (changed lines (59, 271, 272, 273, 274, 275, 276))

```javascript
/*
 * EJS Embedded JavaScript templates
 * Copyright 2112 Matthew Eernisse (mde@fleegix.org)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

'use strict';

/**
 * @file Embedded JavaScript templating engine.
 * @author Matthew Eernisse <mde@fleegix.org>
 * @author Tiancheng "Timothy" Gu <timothygu99@gmail.com>
 * @project EJS
 * @license {@link http://www.apache.org/licenses/LICENSE-2.0 Apache License, Version 2.0}
 */

/**
 * EJS internal functions.
 *
 * Technically this "module" lies in the same file as {@link module:ejs}, for
 * the sake of organization all the private functions re grouped into this
 * module.
 *
 * @module ejs-internal
 * @private
 */

/**
 * Embedded JavaScript templating engine.
 *
 * @module ejs
 * @public
 */

var fs = require('fs');
var path = require('path');
var utils = require('./utils');

var scopeOptionWarned = false;
var _VERSION_STRING = require('../package.json').version;
var _DEFAULT_DELIMITER = '%';
var _DEFAULT_LOCALS_NAME = 'locals';
var _REGEX_STRING = '(<%%|%%>|<%=|<%-|<%_|<%#|<%|%>|-%>|_%>)';
var _OPTS = [ 'cache', 'filename', 'delimiter', 'scope', 'context',
        'debug', 'compileDebug', 'client', '_with', 'root', 'rmWhitespace',
        'strict', 'localsName'];
var _BOM = /^\uFEFF/;

/**
 * EJS template function cache. This can be a LRU object from lru-cache NPM
 * module. By default, it is {@link module:utils.cache}, a simple in-process
 * cache that grows continuously.
 *
 * @type {Cache}
 */

exports.cache = utils.cache;

/**
 * Name of the object containing the locals.
 *
 * This variable is overridden by {@link Options}`.localsName` if it is not
 * `undefined`.
 *
 * @type {String}
 * @public
 */

exports.localsName = _DEFAULT_LOCALS_NAME;

/**
 * Get the path to the included file from the parent file path and the
 * specified path.
 *
 * @param {String}  name     specified path
 * @param {String}  filename parent file path
 * @param {Boolean} isDir    parent file path whether is directory
 * @return {String}
 */
exports.resolveInclude = function(name, filename, isDir) {
  var dirname = path.dirname;
  var extname = path.extname;
  var resolve = path.resolve;
  var includePath = resolve(isDir ? filename : dirname(filename), name);
  var ext = extname(name);
  if (!ext) {
    includePath += '.ejs';
  }
  return includePath;
};

/**
 * Get the path to the included file by Options
 *
 * @param  {String}  path    specified path
 * @param  {Options} options compilation options
 * @return {String}
 */
function getIncludePath(path, options){
  var includePath;
  if (path.charAt(0) == '/') {
    includePath = exports.resolveInclude(path.replace(/^\/*/,''), options.root || '/', true);
  }
  else {
    if (!options.filename) {
      throw new Error('`include` use relative path requires the \'filename\' option.');
    }
    includePath = exports.resolveInclude(path, options.filename);
  }
  return includePath;
}

/**
 * Get the template from a string or a file, either compiled on-the-fly or
 * read from cache (if enabled), and cache the template if needed.
 *
 * If `template` is not set, the file specified in `options.filename` will be
 * read.
 *
 * If `options.cache` is true, this function reads the file from
 * `options.filename` so it must be set prior to calling this function.
 *
 * @memberof module:ejs-internal
 * @param {Options} options   compilation options
 * @param {String} [template] template source
 * @return {(TemplateFunction|ClientFunction)}
 * Depending on the value of `options.client`, either type might be returned.
 * @static
 */

function handleCache(options, template) {
  var func;
  var filename = options.filename;
  var hasTemplate = arguments.length > 1;

  if (options.cache) {
    if (!filename) {
      throw new Error('cache option requires a filename');
    }
    func = exports.cache.get(filename);
    if (func) {
      return func;
    }
    if (!hasTemplate) {
      template = fs.readFileSync(filename).toString().replace(_BOM, '');
    }
  }
  else if (!hasTemplate) {
    // istanbul ignore if: should not happen at all
    if (!filename) {
      throw new Error('Internal EJS error: no file name or template '
                    + 'provided');
    }
    template = fs.readFileSync(filename).toString().replace(_BOM, '');
  }
  func = exports.compile(template, options);
  if (options.cache) {
    exports.cache.set(filename, func);
  }
  return func;
}

/**
 * Get the template function.
 *
 * If `options.cache` is `true`, then the template is cached.
 *
 * @memberof module:ejs-internal
 * @param {String}  path    path for the specified file
 * @param {Options} options compilation options
 * @return {(TemplateFunction|ClientFunction)}
 * Depending on the value of `options.client`, either type might be returned
 * @static
 */

function includeFile(path, options) {
  var opts = utils.shallowCopy({}, options);
  opts.filename = getIncludePath(path, opts);
  return handleCache(opts);
}

/**
 * Get the JavaScript source of an included file.
 *
 * @memberof module:ejs-internal
 * @param {String}  path    path for the specified file
 * @param {Options} options compilation options
 * @return {Object}
 * @static
 */

function includeSource(path, options) {
  var opts = utils.shallowCopy({}, options);
  var includePath;
  var template;
  includePath = getIncludePath(path,opts);
  template = fs.readFileSync(includePath).toString().replace(_BOM, '');
  opts.filename = includePath;
  var templ = new Template(template, opts);
  templ.generateSource();
  return {
    source: templ.source,
    filename: includePath,
    template: template
  };
}

/**
 * Re-throw the given `err` in context to the `str` of ejs, `filename`, and
 * `lineno`.
 *
 * @implements RethrowCallback
 * @memberof module:ejs-internal
 * @param {Error}  err      Error object
 * @param {String} str      EJS source
 * @param {String} filename file name of the EJS file
 * @param {String} lineno   line number of the error
 * @static
 */

function rethrow(err, str, filename, lineno){
  var lines = str.split('\n');
  var start = Math.max(lineno - 3, 0);
  var end = Math.min(lines.length, lineno + 3);
  // Error context
  var context = lines.slice(start, end).map(function (line, i){
    var curr = i + start + 1;
    return (curr == lineno ? ' >> ' : '    ')
      + curr
      + '| '
      + line;
  }).join('\n');

  // Alter exception message
  err.path = filename;
  err.message = (filename || 'ejs') + ':'
    + lineno + '\n'
    + context + '\n\n'
    + err.message;

  throw err;
}

/**
 * Copy properties in data object that are recognized as options to an
 * options object.
 *
 * This is used for compatibility with earlier versions of EJS and Express.js.
 *
 * @memberof module:ejs-internal
 * @param {Object}  data data object
 * @param {Options} opts options object
 * @static
 */

function cpOptsInData(data, opts) {
  _OPTS.forEach(function (p) {
    if (typeof data[p] != 'undefined') {
      // Disallow setting the root opt for includes via a passed data obj
      // Unsanitized, parameterized use of `render` could allow the
      // include directory to be reset, opening up the possibility of
      // remote code execution
      if (p == 'root') {
        return;
      }
      opts[p] = data[p];
    }
  });
}

function stripSemi(str) {
  return str.replace(/;(\s*$)/, '$1');
}

/**
 * Compile the given `str` of ejs into a template function.
 *
 * @param {String}  template EJS template
 *
 * @param {Options} opts     compilation options
 *
 * @return {(TemplateFunction|ClientFunction)}
 * Depending on the value of `opts.client`, either type might be returned.
 * @public
 */

exports.compile = function compile(template, opts) {
  var templ;

  // v1 compat
  // 'scope' is 'context'
  // FIXME: Remove this in a future version
  if (opts && opts.scope) {
    if (!scopeOptionWarned){
      console.warn('`scope` option is deprecated and will be removed in EJS 3');
      scopeOptionWarned = true;
    }
    if (!opts.context) {
      opts.context = opts.scope;
    }
    delete opts.scope;
  }
  templ = new Template(template, opts);
  return templ.compile();
};

/**
 * Render the given `template` of ejs.
 *
 * If you would like to include options but not data, you need to explicitly
 * call this function with `data` being an empty object or `null`.
 *
 * @param {String}   template EJS template
 * @param {Object}  [data={}] template data
 * @param {Options} [opts={}] compilation and rendering options
 * @return {String}
 * @public
 */

exports.render = function (template, d, o) {
  var data = d || {};
  var opts = o || {};

  // No options object -- if there are optiony names
  // in the data, copy them to options
  if (arguments.length == 2) {
    cpOptsInData(data, opts);
  }

  return handleCache(opts, template)(data);
};

/**
 * Render an EJS file at the given `path` and callback `cb(err, str)`.
 *
 * If you would like to include options but not data, you need to explicitly
 * call this function with `data` being an empty object or `null`.
 *
 * @param {String}             path     path to the EJS file
 * @param {Object}            [data={}] template data
 * @param {Options}           [opts={}] compilation and rendering options
 * @param {RenderFileCallback} cb callback
 * @public
 */

exports.renderFile = function () {
  var args = Array.prototype.slice.call(arguments);
  var filename = args.shift();
  var cb = args.pop();
  var data = args.shift() || {};
  var opts = args.pop() || {};
  var result;

  // Don't pollute passed in opts obj with new vals
  opts = utils.shallowCopy({}, opts);

  // No options object -- if there are optiony names
  // in the data, copy them to options
  if (arguments.length == 3) {
    // Express 4
    if (data.settings && data.settings['view options']) {
      cpOptsInData(data.settings['view options'], opts);
    }
    // Express 3 and lower
    else {
      cpOptsInData(data, opts);
    }
  }
  opts.filename = filename;

  try {
    result = handleCache(opts)(data);
  }
  catch(err) {
    return cb(err);
  }
  return cb(null, result);
};

/**
 * Clear intermediate JavaScript cache. Calls {@link Cache#reset}.
 * @public
 */

exports.clearCache = function () {
  exports.cache.reset();
};

function Template(text, opts) {
  opts = opts || {};
  var options = {};
  this.templateText = text;
  this.mode = null;
  this.truncate = false;
  this.currentLine = 1;
  this.source = '';
  this.dependencies = [];
  options.client = opts.client || false;
  options.escapeFunction = opts.escape || utils.escapeXML;
  options.compileDebug = opts.compileDebug !== false;
  options.debug = !!opts.debug;
  options.filename = opts.filename;
  options.delimiter = opts.delimiter || exports.delimiter || _DEFAULT_DELIMITER;
  options.strict = opts.strict || false;
  options.context = opts.context;
  options.cache = opts.cache || false;
  options.rmWhitespace = opts.rmWhitespace;
  options.root = opts.root;
  options.localsName = opts.localsName || exports.localsName || _DEFAULT_LOCALS_NAME;

  if (options.strict) {
    options._with = false;
  }
  else {
    options._with = typeof opts._with != 'undefined' ? opts._with : true;
  }

  this.opts = options;

  this.regex = this.createRegex();
}

Template.modes = {
  EVAL: 'eval',
  ESCAPED: 'escaped',
  RAW: 'raw',
  COMMENT: 'comment',
  LITERAL: 'literal'
};

Template.prototype = {
  createRegex: function () {
    var str = _REGEX_STRING;
    var delim = utils.escapeRegExpChars(this.opts.delimiter);
    str = str.replace(/%/g, delim);
    return new RegExp(str);
  },

  compile: function () {
    var src;
    var fn;
    var opts = this.opts;
    var prepended = '';
    var appended = '';
    var escape = opts.escapeFunction;

    if (!this.source) {
      this.generateSource();
      prepended += '  var __output = [], __append = __output.push.bind(__output);' + '\n';
      if (opts._with !== false) {
        prepended +=  '  with (' + opts.localsName + ' || {}) {' + '\n';
        appended += '  }' + '\n';
      }
      appended += '  return __output.join("");' + '\n';
      this.source = prepended + this.source + appended;
    }

    if (opts.compileDebug) {
      src = 'var __line = 1' + '\n'
          + '  , __lines = ' + JSON.stringify(this.templateText) + '\n'
          + '  , __filename = ' + (opts.filename ?
                JSON.stringify(opts.filename) : 'undefined') + ';' + '\n'
          + 'try {' + '\n'
          + this.source
          + '} catch (e) {' + '\n'
          + '  rethrow(e, __lines, __filename, __line);' + '\n'
          + '}' + '\n';
    }
    else {
      src = this.source;
    }

    if (opts.debug) {
      console.log(src);
    }

    if (opts.client) {
      src = 'escape = escape || ' + escape.toString() + ';' + '\n' + src;
      if (opts.compileDebug) {
        src = 'rethrow = rethrow || ' + rethrow.toString() + ';' + '\n' + src;
      }
    }

    if (opts.strict) {
      src = '"use strict";\n' + src;
    }

    try {
      fn = new Function(opts.localsName + ', escape, include, rethrow', src);
    }
    catch(e) {
      // istanbul ignore else
      if (e instanceof SyntaxError) {
        if (opts.filename) {
          e.message += ' in ' + opts.filename;
        }
        e.message += ' while compiling ejs\n\n';
        e.message += 'If the above error is not helpful, you may want to try EJS-Lint:\n';
        e.message += 'https://github.com/RyanZim/EJS-Lint';
      }
      throw e;
    }

    if (opts.client) {
      fn.dependencies = this.dependencies;
      return fn;
    }

    // Return a callable function which will execute the function
    // created by the source-code, with the passed data as locals
    // Adds a local `include` function which allows full recursive include
    var returnedFn = function (data) {
      var include = function (path, includeData) {
        var d = utils.shallowCopy({}, data);
        if (includeData) {
          d = utils.shallowCopy(d, includeData);
        }
        return includeFile(path, opts)(d);
      };
      return fn.apply(opts.context, [data || {}, escape, include, rethrow]);
    };
    returnedFn.dependencies = this.dependencies;
    return returnedFn;
  },

  generateSource: function () {
    var opts = this.opts;

    if (opts.rmWhitespace) {
      // Have to use two separate replace here as `^` and `$` operators don't
      // work well with `\r`.
      this.templateText =
        this.templateText.replace(/\r/g, '').replace(/^\s+|\s+$/gm, '');
    }

    // Slurp spaces and tabs before <%_ and after _%>
    this.templateText =
      this.templateText.replace(/[ \t]*<%_/gm, '<%_').replace(/_%>[ \t]*/gm, '_%>');

    var self = this;
    var matches = this.parseTemplateText();
    var d = this.opts.delimiter;

    if (matches && matches.length) {
      matches.forEach(function (line, index) {
        var opening;
        var closing;
        var include;
        var includeOpts;
        var includeObj;
        var includeSrc;
        // If this is an opening tag, check for closing tags
        // FIXME: May end up with some false positives here
        // Better to store modes as k/v with '<' + delimiter as key
        // Then this can simply check against the map
        if ( line.indexOf('<' + d) === 0        // If it is a tag
          && line.indexOf('<' + d + d) !== 0) { // and is not escaped
          closing = matches[index + 2];
          if (!(closing == d + '>' || closing == '-' + d + '>' || closing == '_' + d + '>')) {
            throw new Error('Could not find matching close tag for "' + line + '".');
          }
        }
        // HACK: backward-compat `include` preprocessor directives
        if ((include = line.match(/^\s*include\s+(\S+)/))) {
          opening = matches[index - 1];
          // Must be in EVAL or RAW mode
          if (opening && (opening == '<' + d || opening == '<' + d + '-' || opening == '<' + d + '_')) {
            includeOpts = utils.shallowCopy({}, self.opts);
            includeObj = includeSource(include[1], includeOpts);
            if (self.opts.compileDebug) {
              includeSrc =
                  '    ; (function(){' + '\n'
                  + '      var __line = 1' + '\n'
                  + '      , __lines = ' + JSON.stringify(includeObj.template) + '\n'
                  + '      , __filename = ' + JSON.stringify(includeObj.filename) + ';' + '\n'
                  + '      try {' + '\n'
                  + includeObj.source
                  + '      } catch (e) {' + '\n'
                  + '        rethrow(e, __lines, __filename, __line);' + '\n'
                  + '      }' + '\n'
                  + '    ; }).call(this)' + '\n';
            }else{
              includeSrc = '    ; (function(){' + '\n' + includeObj.source +
                  '    ; }).call(this)' + '\n';
            }
            self.source += includeSrc;
            self.dependencies.push(exports.resolveInclude(include[1],
                includeOpts.filename));
            return;
          }
        }
        self.scanLine(line);
      });
    }

  },

  parseTemplateText: function () {
    var str = this.templateText;
    var pat = this.regex;
    var result = pat.exec(str);
    var arr = [];
    var firstPos;

    while (result) {
      firstPos = result.index;

      if (firstPos !== 0) {
        arr.push(str.substring(0, firstPos));
        str = str.slice(firstPos);
      }

      arr.push(result[0]);
      str = str.slice(result[0].length);
      result = pat.exec(str);
    }

    if (str) {
      arr.push(str);
    }

    return arr;
  },

  scanLine: function (line) {
    var self = this;
    var d = this.opts.delimiter;
    var newLineCount = 0;

    function _addOutput() {
      if (self.truncate) {
        // Only replace single leading linebreak in the line after
        // -%> tag -- this is the single, trailing linebreak
        // after the tag that the truncation mode replaces
        // Handle Win / Unix / old Mac linebreaks -- do the \r\n
        // combo first in the regex-or
        line = line.replace(/^(?:\r\n|\r|\n)/, '');
        self.truncate = false;
      }
      else if (self.opts.rmWhitespace) {
        // rmWhitespace has already removed trailing spaces, just need
        // to remove linebreaks
        line = line.replace(/^\n/, '');
      }
      if (!line) {
        return;
      }

      // Preserve literal slashes
      line = line.replace(/\\/g, '\\\\');

      // Convert linebreaks
      line = line.replace(/\n/g, '\\n');
      line = line.replace(/\r/g, '\\r');

      // Escape double-quotes
      // - this will be the delimiter during execution
      line = line.replace(/"/g, '\\"');
      self.source += '    ; __append("' + line + '")' + '\n';
    }

    newLineCount = (line.split('\n').length - 1);

    switch (line) {
    case '<' + d:
    case '<' + d + '_':
      this.mode = Template.modes.EVAL;
      break;
    case '<' + d + '=':
      this.mode = Template.modes.ESCAPED;
      break;
    case '<' + d + '-':
      this.mode = Template.modes.RAW;
      break;
    case '<' + d + '#':
      this.mode = Template.modes.COMMENT;
      break;
    case '<' + d + d:
      this.mode = Template.modes.LITERAL;
      this.source += '    ; __append("' + line.replace('<' + d + d, '<' + d) + '")' + '\n';
      break;
    case d + d + '>':
      this.mode = Template.modes.LITERAL;
      this.source += '    ; __append("' + line.replace(d + d + '>', d + '>') + '")' + '\n';
      break;
    case d + '>':
    case '-' + d + '>':
    case '_' + d + '>':
      if (this.mode == Template.modes.LITERAL) {
        _addOutput();
      }

      this.mode = null;
      this.truncate = line.indexOf('-') === 0 || line.indexOf('_') === 0;
      break;
    default:
        // In script mode, depends on type of tag
      if (this.mode) {
          // If '//' is found without a line break, add a line break.
        switch (this.mode) {
        case Template.modes.EVAL:
        case Template.modes.ESCAPED:
        case Template.modes.RAW:
          if (line.lastIndexOf('//') > line.lastIndexOf('\n')) {
            line += '\n';
          }
        }
        switch (this.mode) {
            // Just executing code
        case Template.modes.EVAL:
          this.source += '    ; ' + line + '\n';
          break;
            // Exec, esc, and output
        case Template.modes.ESCAPED:
          this.source += '    ; __append(escape(' + stripSemi(line) + '))' + '\n';
          break;
            // Exec and output
        case Template.modes.RAW:
          this.source += '    ; __append(' + stripSemi(line) + ')' + '\n';
          break;
        case Template.modes.COMMENT:
              // Do nothing
          break;
            // Literal <%% mode, append as raw output
        case Template.modes.LITERAL:
          _addOutput();
          break;
        }
      }
        // In string mode, just add the output
      else {
        _addOutput();
      }
    }

    if (self.opts.compileDebug && newLineCount) {
      this.currentLine += newLineCount;
      this.source += '    ; __line = ' + this.currentLine + '\n';
    }
  }
};

/**
 * Escape characters reserved in XML.
 *
 * This is simply an export of {@link module:utils.escapeXML}.
 *
 * If `markup` is `undefined` or `null`, the empty string is returned.
 *
 * @param {String} markup Input string
 * @return {String} Escaped string
 * @public
 * @func
 * */
exports.escapeXML = utils.escapeXML;

/**
 * Express.js support.
 *
 * This is an alias for {@link module:ejs.renderFile}, in order to support
 * Express.js out-of-the-box.
 *
 * @func
 */

exports.__express = exports.renderFile;

// Add require support
/* istanbul ignore else */
if (require.extensions) {
  require.extensions['.ejs'] = function (module, flnm) {
    var filename = flnm || /* istanbul ignore next */ module.filename;
    var options = {
      filename: filename,
      client: true
    };
    var template = fs.readFileSync(filename).toString();
    var fn = exports.compile(template, options);
    module._compile('module.exports = ' + fn.toString() + ';', filename);
  };
}

/**
 * Version of EJS.
 *
 * @readonly
 * @type {String}
 * @public
 */

exports.VERSION = _VERSION_STRING;

/* istanbul ignore if */
if (typeof window != 'undefined') {
  window.ejs = exports;
}
```

## `test/ejs.js` (changed lines (171, 756, 921, 922))

```javascript
/* jshint mocha: true */
/* eslint-env node, mocha */

/**
 * Module dependencies.
 */

var ejs = require('..');
var fs = require('fs');
var read = fs.readFileSync;
var assert = require('assert');
var path = require('path');
var LRU = require('lru-cache');

try {
  fs.mkdirSync(__dirname + '/tmp');
} catch (ex) {
  if (ex.code !== 'EEXIST') {
    throw ex;
  }
}

// From https://gist.github.com/pguillory/729616
function hook_stdio(stream, callback) {
  var old_write = stream.write;

  stream.write = (function() {
    return function(string, encoding, fd) {
      callback(string, encoding, fd);
    };
  })(stream.write);

  return function() {
    stream.write = old_write;
  };
}

/**
 * Load fixture `name`.
 */

function fixture(name) {
  return read('test/fixtures/' + name, 'utf8');
}

/**
 * User fixtures.
 */

var users = [];
users.push({name: 'geddy'});
users.push({name: 'neil'});
users.push({name: 'alex'});

suite('ejs.compile(str, options)', function () {
  test('compile to a function', function () {
    var fn = ejs.compile('<p>yay</p>');
    assert.equal(fn(), '<p>yay</p>');
  });

  test('empty input works', function () {
    var fn = ejs.compile('');
    assert.equal(fn(), '');
  });

  test('throw if there are syntax errors', function () {
    try {
      ejs.compile(fixture('fail.ejs'));
    }
    catch (err) {
      assert.ok(err.message.indexOf('compiling ejs') > -1);

      try {
        ejs.compile(fixture('fail.ejs'), {filename: 'fail.ejs'});
      }
      catch (err) {
        assert.ok(err.message.indexOf('fail.ejs') > -1);
        return;
      }
    }
    throw new Error('no error reported when there should be');
  });

  test('allow customizing delimiter local var', function () {
    var fn;
    fn = ejs.compile('<p><?= name ?></p>', {delimiter: '?'});
    assert.equal(fn({name: 'geddy'}), '<p>geddy</p>');

    fn = ejs.compile('<p><:= name :></p>', {delimiter: ':'});
    assert.equal(fn({name: 'geddy'}), '<p>geddy</p>');

    fn = ejs.compile('<p><$= name $></p>', {delimiter: '$'});
    assert.equal(fn({name: 'geddy'}), '<p>geddy</p>');
  });

  test('default to using ejs.delimiter', function () {
    var fn;
    ejs.delimiter = '&';
    fn = ejs.compile('<p><&= name &></p>');
    assert.equal(fn({name: 'geddy'}), '<p>geddy</p>');

    fn = ejs.compile('<p><|= name |></p>', {delimiter: '|'});
    assert.equal(fn({name: 'geddy'}), '<p>geddy</p>');
    delete ejs.delimiter;
  });

  test('have a working client option', function () {
    var fn;
    var str;
    var preFn;
    fn = ejs.compile('<p><%= foo %></p>', {client: true});
    str = fn.toString();
    if (!process.env.running_under_istanbul) {
      eval('var preFn = ' + str);
      assert.equal(preFn({foo: 'bar'}), '<p>bar</p>');
    }
  });

  test('support client mode without locals', function () {
    var fn;
    var str;
    var preFn;
    fn = ejs.compile('<p><%= "foo" %></p>', {client: true});
    str = fn.toString();
    if (!process.env.running_under_istanbul) {
      eval('var preFn = ' + str);
      assert.equal(preFn(), '<p>foo</p>');
    }
  });

  test('not include rethrow() in client mode if compileDebug is false', function () {
    var fn = ejs.compile('<p><%= "foo" %></p>', {
      client: true,
      compileDebug: false
    });
    // There could be a `rethrow` in the function declaration
    assert((fn.toString().match(/rethrow/g) || []).length <= 1);
  });

  test('support custom escape function', function () {
    var customEscape;
    var fn;
    customEscape = function customEscape(str) {
      return !str ? '' : str.toUpperCase();
    };
    fn = ejs.compile('HELLO <%= name %>', {escape: customEscape});
    assert.equal(fn({name: 'world'}), 'HELLO WORLD');
  });

  test('support custom escape function in client mode', function () {
    var customEscape;
    var fn;
    var str;
    customEscape = function customEscape(str) {
      return !str ? '' : str.toUpperCase();
    };
    fn = ejs.compile('HELLO <%= name %>', {escape: customEscape, client: true});
    str = fn.toString();
    if (!process.env.running_under_istanbul) {
      eval('var preFn = ' + str);
      assert.equal(preFn({name: 'world'}), 'HELLO WORLD'); // eslint-disable-line no-undef
    }
  });

  test('strict mode works', function () {
    assert.equal(ejs.render(fixture('strict.ejs'), {}, {strict: true}), 'true');
  });

});

suite('ejs.render(str, data, opts)', function () {
  test('render the template', function () {
    assert.equal(ejs.render('<p>yay</p>'), '<p>yay</p>');
  });

  test('empty input works', function () {
    assert.equal(ejs.render(''), '');
  });

  test('undefined renders nothing escaped', function () {
    assert.equal(ejs.render('<%= undefined %>'), '');
  });

  test('undefined renders nothing raw', function () {
    assert.equal(ejs.render('<%- undefined %>'), '');
  });

  test('null renders nothing escaped', function () {
    assert.equal(ejs.render('<%= null %>'), '');
  });

  test('null renders nothing raw', function () {
    assert.equal(ejs.render('<%- null %>'), '');
  });

  test('zero-value data item renders something escaped', function () {
    assert.equal(ejs.render('<%= 0 %>'), '0');
  });

  test('zero-value data object renders something raw', function () {
    assert.equal(ejs.render('<%- 0 %>'), '0');
  });

  test('accept locals', function () {
    assert.equal(ejs.render('<p><%= name %></p>', {name: 'geddy'}),
        '<p>geddy</p>');
  });

  test('accept locals without using with() {}', function () {
    assert.equal(ejs.render('<p><%= locals.name %></p>', {name: 'geddy'},
                            {_with: false}),
        '<p>geddy</p>');
    assert.throws(function() {
      ejs.render('<p><%= name %></p>', {name: 'geddy'},
                 {_with: false});
    }, /name is not defined/);
  });

  test('accept custom name for locals', function () {
    ejs.localsName = 'it';
    assert.equal(ejs.render('<p><%= it.name %></p>', {name: 'geddy'},
                            {_with: false}),
        '<p>geddy</p>');
    assert.throws(function() {
      ejs.render('<p><%= name %></p>', {name: 'geddy'},
                 {_with: false});
    }, /name is not defined/);
    ejs.localsName = 'locals';
  });

  test('support caching', function () {
    var file = __dirname + '/tmp/render.ejs';
    var options = {cache: true, filename: file};
    var out = ejs.render('<p>Old</p>', {}, options);
    var expected = '<p>Old</p>';
    assert.equal(out, expected);
    // Assert no change, still in cache
    out = ejs.render('<p>New</p>', {}, options);
    assert.equal(out, expected);
  });

  test('support LRU caching', function () {
    var oldCache = ejs.cache;
    var file = __dirname + '/tmp/render.ejs';
    var options = {cache: true, filename: file};
    var out;
    var expected = '<p>Old</p>';

    // Switch to LRU
    ejs.cache = LRU();

    out = ejs.render('<p>Old</p>', {}, options);
    assert.equal(out, expected);
    // Assert no change, still in cache
    out = ejs.render('<p>New</p>', {}, options);
    assert.equal(out, expected);

    // Restore system cache
    ejs.cache = oldCache;
  });

  test('opts.context', function () {
    var ctxt = {foo: 'FOO'};
    var out = ejs.render('<%= this.foo %>', {}, {context: ctxt});
    assert.equal(out, ctxt.foo);
  });
});

suite('ejs.renderFile(path, [data], [options], fn)', function () {
  test('render a file', function(done) {
    ejs.renderFile('test/fixtures/para.ejs', function(err, html) {
      if (err) {
        return done(err);
      }
      assert.equal(html, '<p>hey</p>\n');
      done();
    });
  });

  test('accept locals', function(done) {
    var data =  {name: 'fonebone'};
    var options = {delimiter: '$'};
    ejs.renderFile('test/fixtures/user.ejs', data, options, function(err, html) {
      if (err) {
        return done(err);
      }
      assert.equal(html, '<h1>fonebone</h1>\n');
      done();
    });
  });

  test('accept locals without using with() {}', function(done) {
    var data =  {name: 'fonebone'};
    var options = {delimiter: '$', _with: false};
    var doneCount = 0;
    ejs.renderFile('test/fixtures/user-no-with.ejs', data, options, function(err, html) {
      if (err) {
        if (doneCount === 2) {
          return;
        }
        doneCount = 2;
        return done(err);
      }
      assert.equal(html, '<h1>fonebone</h1>\n');
      doneCount++;
      if (doneCount === 2) {
        done();
      }
    });
    ejs.renderFile('test/fixtures/user.ejs', data, options, function(err) {
      if (!err) {
        if (doneCount === 2) {
          return;
        }
        doneCount = 2;
        return done(new Error('error not thrown'));
      }
      doneCount++;
      if (doneCount === 2) {
        done();
      }
    });
  });

  test('not catch err thrown by callback', function(done) {
    var data =  {name: 'fonebone'};
    var options = {delimiter: '$'};
    var counter = 0;

    var d = require('domain').create();
    d.on('error', function (err) {
      assert.equal(counter, 1);
      assert.equal(err.message, 'Exception in callback');
      done();
    });
    d.run(function () {
      // process.nextTick() needed to work around mochajs/mocha#513
      //
      // tl;dr: mocha doesn't support synchronous exception throwing in
      // domains. Have to make it async. Ticket closed because: "domains are
      // deprecated :D"
      process.nextTick(function () {
        ejs.renderFile('test/fixtures/user.ejs', data, options, function(err) {
          counter++;
          if (err) {
            assert.notEqual(err.message, 'Exception in callback');
            return done(err);
          }
          throw new Error('Exception in callback');
        });
      });
    });
  });

  test('support caching', function (done) {
    var expected = '<p>Old</p>';
    var file = __dirname + '/tmp/renderFile.ejs';
    var options = {cache: true};
    fs.writeFileSync(file, '<p>Old</p>');

    ejs.renderFile(file, {}, options, function (err, out) {
      if (err) {
        done(err);
      }
      fs.writeFileSync(file, '<p>New</p>');
      assert.equal(out, expected);

      ejs.renderFile(file, {}, options, function (err, out) {
        if (err) {
          done(err);
        }
        // Assert no change, still in cache
        assert.equal(out, expected);
        done();
      });
    });
  });

  test('opts.context', function (done) {
    var ctxt = {foo: 'FOO'};
    ejs.renderFile('test/fixtures/with-context.ejs', {}, {context: ctxt}, function(err, html) {
      if (err) {
        return done(err);
      }
      assert.equal(html, ctxt.foo + '\n');
      done();
    });

  });
});

suite('cache specific', function () {
  test('`clearCache` work properly', function () {
    var expected = '<p>Old</p>';
    var file = __dirname + '/tmp/clearCache.ejs';
    var options = {cache: true, filename: file};
    var out = ejs.render('<p>Old</p>', {}, options);
    assert.equal(out, expected);

    ejs.clearCache();

    expected = '<p>New</p>';
    out = ejs.render('<p>New</p>', {}, options);
    assert.equal(out, expected);
  });

  test('`clearCache` work properly, LRU', function () {
    var expected = '<p>Old</p>';
    var oldCache = ejs.cache;
    var file = __dirname + '/tmp/clearCache.ejs';
    var options = {cache: true, filename: file};
    var out;

    ejs.cache = LRU();

    out = ejs.render('<p>Old</p>', {}, options);
    assert.equal(out, expected);
    ejs.clearCache();
    expected = '<p>New</p>';
    out = ejs.render('<p>New</p>', {}, options);
    assert.equal(out, expected);

    ejs.cache = oldCache;
  });

  test('LRU with cache-size 1', function () {
    var oldCache = ejs.cache;
    var options;
    var out;
    var expected;
    var file;

    ejs.cache = LRU(1);

    file = __dirname + '/tmp/render1.ejs';
    options = {cache: true, filename: file};
    out = ejs.render('<p>File1</p>', {}, options);
    expected = '<p>File1</p>';
    assert.equal(out, expected);

    // Same filename, different template, but output
    // should be the same because cache
    file = __dirname + '/tmp/render1.ejs';
    options = {cache: true, filename: file};
    out = ejs.render('<p>ChangedFile1</p>', {}, options);
    expected = '<p>File1</p>';
    assert.equal(out, expected);

    // Different filiename -- output should be different,
    // and previous cache-entry should be evicted
    file = __dirname + '/tmp/render2.ejs';
    options = {cache: true, filename: file};
    out = ejs.render('<p>File2</p>', {}, options);
    expected = '<p>File2</p>';
    assert.equal(out, expected);

    // Entry with first filename should now be out of cache,
    // results should be different
    file = __dirname + '/tmp/render1.ejs';
    options = {cache: true, filename: file};
    out = ejs.render('<p>ChangedFile1</p>', {}, options);
    expected = '<p>ChangedFile1</p>';
    assert.equal(out, expected);

    ejs.cache = oldCache;
  });
});

suite('<%', function () {
  test('without semicolons', function () {
    assert.equal(ejs.render(fixture('no.semicolons.ejs')),
        fixture('no.semicolons.html'));
  });
});

suite('<%=', function () {
  test('should not throw an error with a // comment on the final line', function () {
    assert.equal(ejs.render('<%=\n// a comment\nname\n// another comment %>', {name: '&nbsp;<script>'}),
      '&amp;nbsp;&lt;script&gt;');
  });

  test('escape &amp;<script>', function () {
    assert.equal(ejs.render('<%= name %>', {name: '&nbsp;<script>'}),
        '&amp;nbsp;&lt;script&gt;');
  });

  test('should escape \'', function () {
    assert.equal(ejs.render('<%= name %>', {name: 'The Jones\'s'}),
      'The Jones&#39;s');
  });

  test('should escape &foo_bar;', function () {
    assert.equal(ejs.render('<%= name %>', {name: '&foo_bar;'}),
      '&amp;foo_bar;');
  });

  test('should accept custom function', function() {

    var customEscape = function customEscape(str) {
      return !str ? '' : str.toUpperCase();
    };

    assert.equal(
      ejs.render('<%= name %>', {name: 'The Jones\'s'}, {escape: customEscape}),
      'THE JONES\'S'
    );
  });
});

suite('<%-', function () {
  test('should not throw an error with a // comment on the final line', function () {
    assert.equal(ejs.render('<%-\n// a comment\nname\n// another comment %>', {name: '&nbsp;<script>'}),
        '&nbsp;<script>');
  });

  test('not escape', function () {
    assert.equal(ejs.render('<%- name %>', {name: '<script>'}),
        '<script>');
  });

  test('terminate gracefully if no close tag is found', function () {
    try {
      ejs.compile('<h1>oops</h1><%- name ->');
      throw new Error('Expected parse failure');
    }
    catch (err) {
      assert.ok(err.message.indexOf('Could not find matching close tag for') > -1);
    }
  });
});

suite('%>', function () {
  test('produce newlines', function () {
    assert.equal(ejs.render(fixture('newlines.ejs'), {users: users}),
      fixture('newlines.html'));
  });
  test('works with `-%>` interspersed', function () {
    assert.equal(ejs.render(fixture('newlines.mixed.ejs'), {users: users}),
      fixture('newlines.mixed.html'));
  });
  test('consecutive tags work', function () {
    assert.equal(ejs.render(fixture('consecutive-tags.ejs')),
      fixture('consecutive-tags.html'));
  });
});

suite('-%>', function () {
  test('not produce newlines', function () {
    assert.equal(ejs.render(fixture('no.newlines.ejs'), {users: users}),
      fixture('no.newlines.html'));
  });
  test('stack traces work', function () {
    try {
      ejs.render(fixture('no.newlines.error.ejs'));
    }
    catch (e) {
      if (e.message.indexOf('>> 4| <%= qdata %>') > -1) {
        return;
      }
      throw e;
    }
    throw new Error('Expected ReferenceError');
  });

  test('works with unix style', function () {
    var content = '<ul><% -%>\n'
    + '<% users.forEach(function(user){ -%>\n'
    + '<li><%= user.name -%></li>\n'
    + '<% }) -%>\n'
    + '</ul><% -%>\n';

    var expectedResult = '<ul><li>geddy</li>\n<li>neil</li>\n<li>alex</li>\n</ul>';
    var fn;
    fn = ejs.compile(content);
    assert.equal(fn({users: users}),
      expectedResult);
  });

  test('works with windows style', function () {
    var content = '<ul><% -%>\r\n'
    + '<% users.forEach(function(user){ -%>\r\n'
    + '<li><%= user.name -%></li>\r\n'
    + '<% }) -%>\r\n'
    + '</ul><% -%>\r\n';

    var expectedResult = '<ul><li>geddy</li>\r\n<li>neil</li>\r\n<li>alex</li>\r\n</ul>';
    var fn;
    fn = ejs.compile(content);
    assert.equal(fn({users: users}),
      expectedResult);
  });
});

suite('<%%', function () {
  test('produce literals', function () {
    assert.equal(ejs.render('<%%- "foo" %>'),
      '<%- "foo" %>');
  });
  test('work without an end tag', function () {
    assert.equal(ejs.render('<%%'), '<%');
    assert.equal(ejs.render(fixture('literal.ejs'), {}, {delimiter: ' '}),
      fixture('literal.html'));
  });
});

suite('%%>', function () {
  test('produce literal', function () {
    assert.equal(ejs.render('%%>'),
        '%>');
    assert.equal(ejs.render('  >', {}, {delimiter: ' '}),
        ' >');
  });
});

suite('<%_ and _%>', function () {
  test('slurps spaces and tabs', function () {
    assert.equal(ejs.render(fixture('space-and-tab-slurp.ejs'), {users: users}),
      fixture('space-and-tab-slurp.html'));
  });
});

suite('single quotes', function () {
  test('not mess up the constructed function', function () {
    assert.equal(ejs.render(fixture('single-quote.ejs')),
      fixture('single-quote.html'));
  });
});

suite('double quotes', function () {
  test('not mess up the constructed function', function () {
    assert.equal(ejs.render(fixture('double-quote.ejs')),
      fixture('double-quote.html'));
  });
});

suite('backslashes', function () {
  test('escape', function () {
    assert.equal(ejs.render(fixture('backslash.ejs')),
      fixture('backslash.html'));
  });
});

suite('messed up whitespace', function () {
  test('work', function () {
    assert.equal(ejs.render(fixture('messed.ejs'), {users: users}),
      fixture('messed.html'));
  });
});

suite('exceptions', function () {
  test('produce useful stack traces', function () {
    try {
      ejs.render(fixture('error.ejs'), {}, {filename: 'error.ejs'});
    }
    catch (err) {
      assert.equal(err.path, 'error.ejs');
      assert.equal(err.stack.split('\n').slice(0, 8).join('\n'), fixture('error.out'));
      return;
    }
    throw new Error('no error reported when there should be');
  });

  test('not include fancy stack info if compileDebug is false', function () {
    try {
      ejs.render(fixture('error.ejs'), {}, {
        filename: 'error.ejs',
        compileDebug: false
      });
    }
    catch (err) {
      assert.ok(!err.path);
      assert.notEqual(err.stack.split('\n').slice(0, 8).join('\n'), fixture('error.out'));
      return;
    }
    throw new Error('no error reported when there should be');
  });

  var unhook = null;
  test('log JS source when debug is set', function (done) {
    var out = '';
    var needToExit = false;
    unhook = hook_stdio(process.stdout, function (str) {
      out += str;
      if (needToExit) {
        return;
      }
      if (out.indexOf('__output')) {
        needToExit = true;
        unhook();
        unhook = null;
        return done();
      }
    });
    ejs.render(fixture('hello-world.ejs'), {}, {debug: true});
  });
  teardown(function() {
    if (!unhook) {
      return;
    }
    unhook();
    unhook = null;
  });
});

suite('rmWhitespace', function () {
  test('works', function () {
    assert.equal(ejs.render(fixture('rmWhitespace.ejs'), {}, {rmWhitespace: true}),
        fixture('rmWhitespace.html'));
  });
});

suite('include()', function () {
  test('include ejs', function () {
    var file = 'test/fixtures/include-simple.ejs';
    assert.equal(ejs.render(fixture('include-simple.ejs'), {}, {filename: file}),
        fixture('include-simple.html'));
  });

  test('include ejs fails without `filename`', function () {
    try {
      ejs.render(fixture('include-simple.ejs'));
    }
    catch (err) {
      assert.ok(err.message.indexOf('requires the \'filename\' option') > -1);
      return;
    }
    throw new Error('expected inclusion error');
  });

  test('strips BOM', function () {
    assert.equal(
      ejs.render('<%- include("fixtures/includes/bom.ejs") %>',
        {}, {filename: path.join(__dirname, 'f.ejs')}),
      '<p>This is a file with BOM.</p>\n');
  });

  test('include ejs with locals', function () {
    var file = 'test/fixtures/include.ejs';
    assert.equal(ejs.render(fixture('include.ejs'), {pets: users}, {filename: file, delimiter: '@'}),
        fixture('include.html'));
  });

  test('include ejs with absolute path and locals', function () {
    var file = 'test/fixtures/include-abspath.ejs';
    assert.equal(ejs.render(fixture('include-abspath.ejs'),
      {dir: path.join(__dirname, 'fixtures'), pets: users, path: path},
      {filename: file, delimiter: '@'}),
        fixture('include.html'));
  });

  test('include ejs with set root path', function () {
    var file = 'test/fixtures/include-root.ejs';
    var viewsPath = path.join(__dirname, 'fixtures');
    assert.equal(ejs.render(fixture('include-root.ejs'), {pets: users}, {filename: file, delimiter: '@',root:viewsPath}),
      fixture('include.html'));

  });

  test('work when nested', function () {
    var file = 'test/fixtures/menu.ejs';
    assert.equal(ejs.render(fixture('menu.ejs'), {pets: users}, {filename: file}),
        fixture('menu.html'));
  });

  test('work with a variable path', function () {
    var file = 'test/fixtures/menu_var.ejs';
    var includePath = 'includes/menu-item';
    assert.equal(ejs.render(fixture('menu.ejs'), {pets: users, varPath:  includePath}, {filename: file}),
      fixture('menu.html'));
  });

  test('include arbitrary files as-is', function () {
    var file = 'test/fixtures/include.css.ejs';
    assert.equal(ejs.render(fixture('include.css.ejs'), {pets: users}, {filename: file}),
        fixture('include.css.html'));
  });

  test('pass compileDebug to include', function () {
    var file = 'test/fixtures/include.ejs';
    var fn;
    fn = ejs.compile(fixture('include.ejs'), {
      filename: file,
      delimiter: '@',
      compileDebug: false
    });
    try {
      // Render without a required variable reference
      fn({foo: 'asdf'});
    }
    catch(e) {
      assert.equal(e.message, 'pets is not defined');
      assert.ok(!e.path);
      return;
    }
    throw new Error('no error reported when there should be');
  });

  test('is dynamic', function () {
    fs.writeFileSync(__dirname + '/tmp/include.ejs', '<p>Old</p>');
    var file = 'test/fixtures/include_cache.ejs';
    var options = {filename: file};
    var out = ejs.compile(fixture('include_cache.ejs'), options);
    assert.equal(out(), '<p>Old</p>\n');

    fs.writeFileSync(__dirname + '/tmp/include.ejs', '<p>New</p>');
    assert.equal(out(), '<p>New</p>\n');
  });

  test('support caching', function () {
    fs.writeFileSync(__dirname + '/tmp/include.ejs', '<p>Old</p>');
    var file = 'test/fixtures/include_cache.ejs';
    var options = {cache: true, filename: file};
    var out = ejs.render(fixture('include_cache.ejs'), {}, options);
    var expected = fixture('include_cache.html');
    assert.equal(out, expected);
    out = ejs.render(fixture('include_cache.ejs'), {}, options);
    // No change, still in cache
    assert.equal(out, expected);
    fs.writeFileSync(__dirname + '/tmp/include.ejs', '<p>New</p>');
    out = ejs.render(fixture('include_cache.ejs'), {}, options);
    assert.equal(out, expected);
  });

});

suite('preprocessor include', function () {
  test('work', function () {
    var file = 'test/fixtures/include_preprocessor.ejs';
    assert.equal(ejs.render(fixture('include_preprocessor.ejs'), {pets: users}, {filename: file, delimiter: '@'}),
        fixture('include_preprocessor.html'));
  });

  test('no false positives', function () {
    assert.equal(ejs.render('<% %> include foo <% %>'), ' include foo ');
  });

  test('fails without `filename`', function () {
    try {
      ejs.render(fixture('include_preprocessor.ejs'), {pets: users}, {delimiter: '@'});
    }
    catch (err) {
      assert.ok(err.message.indexOf('requires the \'filename\' option') > -1);
      return;
    }
    throw new Error('expected inclusion error');
  });

  test('strips BOM', function () {
    assert.equal(
      ejs.render('<% include fixtures/includes/bom.ejs %>',
        {}, {filename: path.join(__dirname, 'f.ejs')}),
      '<p>This is a file with BOM.</p>\n');
  });

  test('work when nested', function () {
    var file = 'test/fixtures/menu_preprocessor.ejs';
    assert.equal(ejs.render(fixture('menu_preprocessor.ejs'), {pets: users}, {filename: file}),
        fixture('menu_preprocessor.html'));
  });

  test('tracks dependency correctly', function () {
    var file = 'test/fixtures/menu_preprocessor.ejs';
    var fn = ejs.compile(fixture('menu_preprocessor.ejs'), {filename: file});
    assert(fn.dependencies.length);
  });

  test('include arbitrary files as-is', function () {
    var file = 'test/fixtures/include_preprocessor.css.ejs';
    assert.equal(ejs.render(fixture('include_preprocessor.css.ejs'), {pets: users}, {filename: file}),
        fixture('include_preprocessor.css.html'));
  });

  test('pass compileDebug to include', function () {
    var file = 'test/fixtures/include_preprocessor.ejs';
    var fn;
    fn = ejs.compile(fixture('include_preprocessor.ejs'), {
      filename: file,
      delimiter: '@',
      compileDebug: false
    });
    try {
      // Render without a required variable reference
      fn({foo: 'asdf'});
    }
    catch(e) {
      assert.equal(e.message, 'pets is not defined');
      assert.ok(!e.path);
      return;
    }
    throw new Error('no error reported when there should be');
  });

  test('is static', function () {
    fs.writeFileSync(__dirname + '/tmp/include_preprocessor.ejs', '<p>Old</p>');
    var file = 'test/fixtures/include_preprocessor_cache.ejs';
    var options = {filename: file};
    var out = ejs.compile(fixture('include_preprocessor_cache.ejs'), options);
    assert.equal(out(), '<p>Old</p>\n');

    fs.writeFileSync(__dirname + '/tmp/include_preprocessor.ejs', '<p>New</p>');
    assert.equal(out(), '<p>Old</p>\n');
  });

  test('support caching', function () {
    fs.writeFileSync(__dirname + '/tmp/include_preprocessor.ejs', '<p>Old</p>');
    var file = 'test/fixtures/include_preprocessor_cache.ejs';
    var options = {cache: true, filename: file};
    var out = ejs.render(fixture('include_preprocessor_cache.ejs'), {}, options);
    var expected = fixture('include_preprocessor_cache.html');
    assert.equal(out, expected);
    fs.writeFileSync(__dirname + '/tmp/include_preprocessor.ejs', '<p>New</p>');
    out = ejs.render(fixture('include_preprocessor_cache.ejs'), {}, options);
    assert.equal(out, expected);
  });

  test('whitespace slurp and rmWhitespace work', function() {
    var file = 'test/fixtures/include_preprocessor_line_slurp.ejs';
    var template = fixture('include_preprocessor_line_slurp.ejs');
    var expected = fixture('include_preprocessor_line_slurp.html');
    var options = {rmWhitespace: true, filename: file};
    assert.equal(ejs.render(template, options),
        expected);
  });

});

suite('comments', function () {
  test('fully render with comments removed', function () {
    assert.equal(ejs.render(fixture('comments.ejs')),
        fixture('comments.html'));
  });
});

suite('require', function () {

  // Only works with inline/preprocessor includes
  test('allow ejs templates to be required as node modules', function () {
    var file = 'test/fixtures/include_preprocessor.ejs';
    var template = require(__dirname + '/fixtures/menu_preprocessor.ejs');
    if (!process.env.running_under_istanbul) {
      assert.equal(template({filename: file, pets: users}),
          fixture('menu_preprocessor.html'));
    }
  });
});

suite('examples', function () {
  function noop () {}
  fs.readdirSync('examples').forEach(function (f) {
    if (!/\.js$/.test(f)) {
      return;
    }
    suite(f, function () {
      test('doesn\'t throw any errors', function () {
        var stderr = hook_stdio(process.stderr, noop);
        var stdout = hook_stdio(process.stdout, noop);
        try {
          require('../examples/' + f);
        }
        catch (ex) {
          stdout();
          stderr();
          throw ex;
        }
        stdout();
        stderr();
      });
    });
  });
});
```
