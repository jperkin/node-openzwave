node-openzwave
==============

This is a node.js add-on which wraps the [Open
Z-Wave](https://code.google.com/p/open-zwave/) library to provide access to a
Z-Wave network from JavaScript.

It is currently non-functional, but does at least build libopenzwave as an
add-on with a small wrapper which is able to instantiate a new manager and set
some parameters.

It is currently only working on OSX and Linux, as Open Z-Wave itself only
supports OSX, Linux and Windows.  I hope to add support for other POSIX systems
at some point.

## Install

```sh
$ npm install openzwave
```

## Usage

A brief example which attempts to open a Z-Wave device attached via USB serial.

```js
var OpenZWave = require('openzwave').OpenZWave;
var os = require('os');

var usbdev;
switch (os.platform()) {
case 'darwin':
  usbdev = '/dev/cu.usbserial';
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
```
