/*
 * OpenZWave test program
 */

var OpenZWave = require('./lib/openzwave.js').OpenZWave;

var ozw = new OpenZWave('/dev/cu.Bluetooth-PDA-Sync', {
  consoleOutput: true,
  saveLogLevel: 10
});

ozw.on('open', function() {
  console.log('connected');
  ozw.on('data', function(data) {
    console.log('data: ' + data);
  });
});

console.log('running');
