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

var addon = require(__dirname + '/../build/Release/openzwave.node').Emitter;
var events = require('events');

/*
 * Extend prototype.
 */
function inherits(target, source) {
	for (var k in source.prototype)
		target.prototype[k] = source.prototype[k];
}

/*
 * Default options
 */
var _options = {
	modpath: __dirname,
	consoleoutput: false,
	logging: false,
	saveconfig: false,
	driverattempts: 3,
	pollinterval: 500,
	suppressrefresh: true,
	validatevalues: true,
};
var ZWave = function(path, options) {
	options = options || {};
	options.modpath = options.hasOwnProperty("modpath") ? options.modpath : _options.modpath;
	options.consoleoutput = options.hasOwnProperty("consoleoutput") ? options.consoleoutput : _options.consoleoutput;
	options.logging = options.hasOwnProperty("logging") ? options.logging : _options.logging;
	options.saveconfig = options.hasOwnProperty("saveconfig") ? options.saveconfig : _options.saveconfig;
	options.driverattempts = options.hasOwnProperty("driverattempts") ? options.driverattempts : _options.driverattempts;
	options.pollinterval = options.hasOwnProperty("pollinterval") ? options.pollinterval : _options.pollinterval;
	options.suppressrefresh = options.hasOwnProperty("suppressrefresh") ? options.suppressrefresh : _options.suppressrefresh;
	options.validatevalues = options.hasOwnProperty('validatevalues') ? options.validatevalues : _options.validatevalues;
	this.path = path;
	this.addon = new addon(options);
	this.addon.emit = this.emit.bind(this);
};

inherits(ZWave, events.EventEmitter);
inherits(ZWave, addon);

ZWave.prototype.connect = function() {
	this.addon.connect(this.path);
};

ZWave.prototype.disconnect = function() {
	this.addon.disconnect(this.path);
};

module.exports = ZWave;
