/*
 * OpenZWave test program.
 */

var OpenZWave = require('./lib/openzwave.js');
var zwaves = [];

var z = function(comName) {

var zwave = new OpenZWave(comName, {
	saveconfig: true,
});
zwaves.push(zwave);

var nodes = [];

zwave.on('driver ready', function(homeid) {
	console.log('scanning homeid=0x' + homeid.toString(16) + ' on ' + comName + '...');
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
		name: '',
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
        var val;
	nodes[nodeid]['manufacturer'] = nodeinfo.manufacturer;
	nodes[nodeid]['product'] = nodeinfo.product;
	nodes[nodeid]['type'] = nodeinfo.type;
	nodes[nodeid]['name'] = nodeinfo.name;
	nodes[nodeid]['loc'] = nodeinfo.loc;
	console.log('node%d: %s, %s', nodeid,
		    nodeinfo.manufacturer,
		    nodeinfo.product);
	console.log('node%d: name="%s", type="%s", location="%s"', nodeid,
		    nodeinfo.name,
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
};	// var z = function(comName) {

OpenZWave.devices(function(err, info) {
	var i;
	if (!!err) return console.log('OpenZWave.devices: ' + err.message);
	for (i = 0; i < info.length; i++) z(info[i].comName);
});

process.on('SIGINT', function() {
	var i;
	console.log('disconnecting...');
	for (i = 0; i < zwaves.length; i++) zwaves[i].disconnect();
	process.exit();
});
