/*
 * OpenZWave test program
 */

var OZW = require('./lib/openzwave.js').Emitter;

var zwave = new OZW();

zwave.on('connected', function() {
	console.log('connected');

	zwave.on('driver ready', function() {
		console.log('driver ready');
	});

	zwave.on('node added', function(nodeid) {
		console.log('new node discovered: ' + nodeid);
	});

	zwave.on('node ready', function(nodeid, nodeinfo) {
		console.log('node ' + nodeid + ' ready: (' +
			    nodeinfo.manufacturer + ' ' +
			    nodeinfo.product + ', ' +
			    nodeinfo.type + ', ' +
			    nodeinfo.location + ')');
	});
});

zwave.connect();

process.on('SIGINT', function() {
	console.log('disconnecting');
	zwave.disconnect();
	process.exit();
});

console.log('running');
