wisn
====

A system for wireless device localisation

wisn - the client
=================

This application is intended to run on a small device with a wireless interface
such as a raspberry pi, beaglebone, access point, etc. wisn will sniff on all
2.4 GHz wireless channels and store received signal strength values from all
devices it receives a packet from. This data is transmitted to the server for
further processing. The client is reliant on an MQTT broker to sent data to the
server.

Dependencies:
libpcap, libmosquitto, libpthread

wisn_server - the server
=================

This application is intended to run on a PC. wisn_server collects data from the
wisn clients and processes it to calculate distances from nodes and then a
position using multilateration. The server is reliant on an MQTT broker and a
mongodb database to store some of the data.

Dependencies:
libmosquitto, GNU Scientific Library (GSL), libmongoc, libbson, libpthread


wisnWebserver - the web server
==============================

This is a node.js web application that enables a visual representation of the
data collected by the wisn clients and server. The webserver communicates with
a wisn server using MQTT and passes some data through mongodb as well.

Dependencies:
node.js, express.js, mqtt.js, socket.io, mongodb
