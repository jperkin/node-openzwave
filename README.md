node-openzwave
==============

This is a node.js add-on which wraps the
[OpenZWave](https://code.google.com/p/open-zwave/) library to provide access to
a ZWave network from JavaScript.

It is currently non-functional, but does build an add-on which contains the
openzwave library and is able to instantiate a new manager.

## Example

A brief example which attempts to open a Z-Wave device attached via USB serial.

``js
var OpenZWave = require('openzwave').OpenZWave;

var ozw = new OpenZWave('/dev/cu.usbserial', {
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
``
