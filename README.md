node-openzwave
==============

This is a node.js add-on which wraps the [Open
Z-Wave](https://code.google.com/p/open-zwave/) library to provide access to a
Z-Wave network from JavaScript.

It is currently able to scan a Z-Wave network and report on connected devices.

The ability to read and write value data is planned.

## Install

```sh
$ npm install openzwave
```

## Usage

A test program is included which will connect to a USB Z-Wave stick on
`/dev/ttyUSB0` and scan the network, printing out attached devices.

```sh
$ node test.js 2>/dev/null
connected
running
driver ready
new node discovered: 1
new node discovered: 10
new node discovered: 11
new node discovered: 12
new node discovered: 13
node 1 ready: (Aeon Labs Z-Stick S2, 0002, )
node 11 ready: (Everspring AD142 Plug-in Dimmer Module, 0003, )
node 12 ready: (Wenzhou TKB Control System Unknown: type=0101, id=0103, 0101, )
node 13 ready: (Wenzhou TKB Control System Unknown: type=0101, id=0103, 0101, )
node 10 ready: (Popp / Duwi ZW ESJ Blind Control, 4001, )
scan complete
```

Remove `2>/dev/null` to get verbose output of all incoming notification types.
