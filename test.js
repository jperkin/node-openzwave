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
});

zwave.connect();

process.on('SIGINT', function() {
	console.log('disconnecting');
	zwave.disconnect();
	process.exit();
});

console.log('running');
