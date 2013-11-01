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
	driverattempts: 3
}
var ZWave = function(path, options) {
	options = options || {};
	options.modpath = options.modpath || _options.modpath;
	options.consoleoutput = options.consoleoutput || _options.consoleoutput;
	options.logging = options.logging || _options.logging;
	options.saveconfig = options.saveconfig || _options.saveconfig;
	options.driverattempts = options.driverattempts || _options.driverattempts;
	this.path = path;
	this.addon = new addon(options);
	this.addon.emit = this.emit.bind(this);
}

inherits(ZWave, events.EventEmitter);
inherits(ZWave, addon);

ZWave.prototype.connect = function() {
	this.addon.connect(this.path);
}

ZWave.prototype.disconnect = function() {
	this.addon.disconnect(this.path);
}

ZWave.constants =
{ commandClass:
  { alarm                           : 0x71
  , applicationStatus               : 0x22
  , association                     : 0x85
  , associationCommandConfiguration : 0x9b
  , basic                           : 0x20
  , basicWindowCovering             : 0x50
  , battery                         : 0x80
  , crc16Encap                      : 0x56
  , climateControlSchedule          : 0x46
  , clock                           : 0x81
  , configuration                   : 0x70
  , controllerReplication           : 0x21
  , energyProduction                : 0x90
  , hail                            : 0x82
  , indicator                       : 0x87
  , language                        : 0x89
  , lock                            : 0x76
  , manufacturerSpecific            : 0x72
  , meter                           : 0x32
  , meterPulse                      : 0x35
  , multiCmd                        : 0x8f
  , multiInstanceChannel            : 0x60
  , multiInstanceAssociation        : 0x8e
  , noOperation                     : 0x00
  , powerlevel                      : 0x73
  , proprietary                     : 0x88
  , protection                      : 0x75
  , sceneActivation                 : 0x2B
  , sensorAlarm                     : 0x9c
  , sensorBinary                    : 0x30
  , sensorMultilevel                : 0x31
  , switchAll                       : 0x27
  , switchBinary                    : 0x25
  , switchMultilevel                : 0x26
  , switchToggleBinary              : 0x28
  , switchToggleMultilevel          : 0x29
  , thermostatFanMode               : 0x44
  , thermostatFanState              : 0x45
  , thermostatMode                  : 0x40
  , thermostatOperatingState        : 0x42
  , thermostatSetpoint              : 0x43
  , userCode                        : 0x63
  , version                         : 0x86
  , wakeUp                          : 0x84
  }
, notificationType:
  { valueAdded                      :    0
  , valueRemoved                    :    1
  , valueChanged                    :    2
  , valueRefreshed                  :    3
  , group                           :    4
  , nodeNew                         :    5
  , nodeAdded                       :    6
  , nodeRemoved                     :    7
  , nodeProtocolInfo                :    8
  , nodeNaming                      :    9
  , nodeEvent                       :   10
  , pollingDisabled                 :   11
  , pollingEnabled                  :   12
  , sceneEvent                      :   13
  , createButton                    :   14
  , deleteButton                    :   15
  , buttonOn                        :   16
  , buttonOff                       :   17
  , driverReady                     :   18
  , driverFailed                    :   19
  , driverReset                     :   20
  , essentialNodeQueriesComplete    :   21
  , nodeQueriesComplete             :   22
  , awakeNodesQueried               :   23
  , allNodesQueriedSomeDead         :   24
  , allNodesQueried                 :   25
  , unknown                         :   26
  }
, notificationCode:
  { msgComplete                     :    0
  , timeout                         :    1
  , noOperation                     :    2
  , awake                           :    3
  , sleep                           :    4
  , dead                            :    5
  , alive                           :    6
  }
}

module.exports = ZWave;
