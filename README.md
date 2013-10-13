node-openzwave
==============

This is a node.js add-on which wraps the [Open
Z-Wave](https://code.google.com/p/open-zwave/) library to provide access to a
Z-Wave network from JavaScript.

It is currently able to scan a Z-Wave network, report on connected devices,
monitor the network for changes, and has rudimentary write support.

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
scanning...
node1: Aeon Labs, Z-Stick S2
node1: type="0002", location=""
node12: switch=false->true
node13: switch=false->true
node11: level=0->10
node12: Wenzhou TKB Control System, Unknown: type=0101, id=0103
node12: type="0101", location=""
node12: switch=true
node13: Wenzhou TKB Control System, Unknown: type=0101, id=0103
node13: type="0101", location=""
node13: switch=true
node10: switch=false->true
node10: level=0->99
node11: Everspring, AD142 Plug-in Dimmer Module
node11: type="0003", location=""
node11: level=10
node10: Popp / Duwi, ZW ESJ Blind Control
node10: type="4001", location=""
node10: level=99
node10: switch=true
scan complete, hit ^C to finish.
^Cdisconnecting...
```

Remove `2>/dev/null` to get verbose output of all incoming notification types
and additional debug information.

To set a device level, use (for example):

```js
/*
 * Set node 7 to full.  Works for COMMAND_CLASS_SWITCH_MULTILEVEL devices.
 */
zwave.setLevel(7, 99);
```
