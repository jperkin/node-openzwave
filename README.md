node-openzwave
==============

This is a node.js add-on which wraps the [Open
Z-Wave](https://code.google.com/p/open-zwave/) library to provide access to a
Z-Wave network from JavaScript.

It is currently non-functional, but does at least build libopenzwave as an
add-on with a small wrapper which is able to instantiate a new manager and set
some parameters.

## Example

A brief example which attempts to open a Z-Wave device attached via USB serial.

```js
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
```
