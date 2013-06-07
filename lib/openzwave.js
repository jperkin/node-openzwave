/*
 * Copyright (c) 2013 Jonathan Perkin <jonathan@perkin.org.uk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

var libozw = require('../build/Release/openzwave')
var util = require('util')
var events = require('events')

function OpenZWave (path, options, openImmediately) {
  options = options || {};
  openImmediately = (openImmediately === undefined || openImmediately === null) ? true : openImmediately;

  var self = this;

  events.EventEmitter.call(this);

  options.consoleOutput = options.consoleOutput || false;
  options.saveLogLevel = options.saveLogLevel || 1; // LogLevel_Always

  this.path = path;
  this.options= options;

  if (openImmediately) {
    process.nextTick(function () {
      self.open();
    });
  }
}
util.inherits(OpenZWave, events.EventEmitter)

OpenZWave.prototype.open = function (callback) {
  var self = this;
  libozw.open(this.path, this.options, function (err) {
    if (err) {
      self.emit('error', err);
      if (callback)
        callback(err);
      return;
    }
    self.emit('open');
    if (callback)
      callback(err);
  });
}

module.exports.OpenZWave = OpenZWave;
