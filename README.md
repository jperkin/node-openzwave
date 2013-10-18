node-openzwave
==============

This is a node.js add-on which wraps the [Open
Z-Wave](https://code.google.com/p/open-zwave/) library to provide access to a
Z-Wave network from JavaScript.

It is currently able to scan a Z-Wave network, report on connected devices,
monitor the network for changes, and has rudimentary write support.

## Install

```sh
$ npm install openzwave
```

## API

Start by loading the addon and creating a new instance, specifying a path to
the USB device:

```js
var OZW = require('openzwave');
var zwave = new OZW('/dev/ttyUSB0');
```

An optional object can be passed at creation time to alter the behavior of the
ZWave module.  The options currently supported and their defaults are:

```js
var zwave = new OZW('/dev/ttyUSB0', {
        logging: false,           // enable logging to OZW_Log.txt
        consoleoutput: false,     // copy logging to the console
        saveconfig: false,        // write an XML network layout
        driverattempts: 3,        // try this many times before giving up
});
```

The rest of the API is split into Functions and Events.  Messages from the
Z-Wave network are handled by `EventEmitter`, and you will need to listen for
specific events to correctly map the network.

### Functions

Connecting to the network:

```js
zwave.connect();     // initialise and start a new driver.
zwave.disconnect();  // disconnect from the current connection
```

Modifying device state:

```js
/*
 * Set a multi-level device to the specified level (between 0-99).
 */
zwave.setLevel(nodeid, level);

/*
 * Turn a binary switch on/off.
 */
zwave.switchOn(nodeid);
zwave.switchOff(nodeid);
```

Writing to device metadata (stored on the device itself):

```js
zwave.setLocation(nodeid, location);    // arbitrary location string
zwave.setName(nodeid, name);            // arbitrary name string
```

### Events

The supported events are:

#### `.on('connected', function(){})`

We have connected to an OpenZWave node.

#### `.on('driver ready', function(homeid){})`

The OpenZWave driver has initialised and scanning has started.  Returns a
unique `homeid` which identifies this particular network.

#### `.on('driver failed', function(){})`

The OpenZWave driver failed to initialise.

#### `.on('node added', function(nodeid){})`

A new node has been found on the network.  At this point you can allocate
resources to hold information about this node.

#### `.on('value added', function(nodeid, type, value){})`

A new value has been discovered.  Values are associated with a particular node,
and are the parts of the device you can control to switch on/off or change a
level.

We currently support two value types:

* `switch`: A binary switch, with value set to either `true` (on) or `false`
  (off).
* `level`: A multi-level device where value is set to an integer between `0`
  and `99`.

Note that the first value received may not be an accurate reflection of the
device state, and you should monitor for `value changed` events (described
below) to properly monitor for device values.

#### `.on('value changed', function(nodeid, type, value){})`

A value has changed.  Use this to keep track of value state across the network.
When values are first discovered, the module enables polling on all devices so
that they will send out change messages.

#### `.on('node ready', function(nodeid, nodeinfo){})`

A node is now ready for operation, and information about the node is available
in the `nodeinfo` object:

* `nodeinfo.manufacturer`
* `nodeinfo.product`
* `nodeinfo.type`
* `nodeinfo.loc` (location, renamed to avoid `location` keyword).

#### `.on('scan complete', function(){})`

The initial network scan has finished.

## Example

The test program below connects to a Z-Wave network, scans for all nodes and
values, and prints out information about the network.  It will then continue to
scan for changes until the user hits `^C`.

```js
/*
 * OpenZWave test program.
 */

var OpenZWave = require('openzwave');

var zwave = new OpenZWave('/dev/ttyUSB0', {
	saveconfig: true,
});
var nodes = [];

zwave.on('driver ready', function() {
	console.log('scanning...');
});

zwave.on('driver failed', function() {
	console.log('failed to start driver');
	zwave.disconnect();
	process.exit();
});

zwave.on('node added', function(nodeid) {
	nodes[nodeid] = {
		manufacturer: '',
		product: '',
		type: '',
		loc: '',
		values: {},
	};
});

zwave.on('value added', function(nodeid, type, value) {
	nodes[nodeid]['values'][type] = value;
});

zwave.on('value changed', function(nodeid, type, value) {
	if (nodes[nodeid]['values'][type] != value) {
		console.log('node%d: %s=%s->%s', nodeid, type,
			    nodes[nodeid]['values'][type], value);
		nodes[nodeid]['values'][type] = value;
	}
});

zwave.on('node ready', function(nodeid, nodeinfo) {
	nodes[nodeid]['manufacturer'] = nodeinfo.manufacturer;
	nodes[nodeid]['product'] = nodeinfo.product;
	nodes[nodeid]['type'] = nodeinfo.type;
	nodes[nodeid]['loc'] = nodeinfo.loc;
	console.log('node%d: %s, %s', nodeid,
		    nodeinfo.manufacturer,
		    nodeinfo.product);
	console.log('node%d: type="%s", location="%s"', nodeid,
		    nodeinfo.type,
		    nodeinfo.loc);
	for (val in nodes[nodeid]['values']) {
		console.log('node%d: %s=%s', nodeid, val, nodes[nodeid]['values'][val]);
	}
});

zwave.on('scan complete', function() {
	console.log('scan complete, hit ^C to finish.');
});

zwave.connect();

process.on('SIGINT', function() {
	console.log('disconnecting...');
	zwave.disconnect();
	process.exit();
});
```

Sample output from this program:

```sh
$ node test.js 2>/dev/null
scanning...
node1: Aeon Labs, Z-Stick S2
node1: type="Static PC Controller", location=""
node12: switch=false->true
node13: switch=false->true
node11: level=0->9
node12: Wenzhou TKB Control System, Unknown: type=0101, id=0103
node12: type="Binary Power Switch", location=""
node12: switch=true
node13: Wenzhou TKB Control System, Unknown: type=0101, id=0103
node13: type="Binary Power Switch", location=""
node13: switch=true
node10: switch=false->true
node10: level=0->99
node11: Everspring, AD142 Plug-in Dimmer Module
node11: type="Multilevel Power Switch", location=""
node11: level=9
node10: Popp / Duwi, ZW ESJ Blind Control
node10: type="Multiposition Motor", location=""
node10: level=99
node10: switch=true
scan complete, hit ^C to finish.
^Cdisconnecting...
```

Remove `2>/dev/null` to get verbose output of all incoming notification types
and additional debug information.

## License

The [Open Z-Wave](https://code.google.com/p/open-zwave/) library that this
module heavily relies upon is licensed under the GPLv3.

Everything else (all the bits that I have written) is under the vastly more
sensible ISC license.
