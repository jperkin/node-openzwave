/*
 * OpenZWave test program
 */

var OpenZWave = require('./lib/openzwave.js').OpenZWave;

var usbdev;
switch (os.platform()) {
case 'darwin':
  usbdev = '/dev/cu.Bluetooth-PDA-Sync';
  break;
case 'linux':
  usbdev = '/dev/ttyUSB0';
  break;
}

var ozw = new OpenZWave(usbdev, {
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
