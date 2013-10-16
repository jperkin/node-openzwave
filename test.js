/*
 * OpenZWave test program
 */

var OpenZWave = require('./lib/openzwave.js');

var zwave = new OpenZWave('/dev/ttyUSB0',
    {logging: true, consoleoutput: true});
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
