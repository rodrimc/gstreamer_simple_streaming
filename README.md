# GStreamer Simple Streaming
This is a simple project that provides an implementation of a streaming server and client using GStreamer. The server takes a URI as input and streams it through a TCP socket.

## How to use
To run the server, a URI, a network interface and a port should be specified:

`./tcp_server -f <URI> -i <interface> -p <port>`

To run the client, it should be specified the server address and port:

`./tcp_client -s <server_address> -p <port>`

## Dependencies
This code depends of [GStreamer 1.x](http://gstreamer.freedesktop.org/ "GStreamer") and [GLib 2.0](https://developer.gnome.org/glib/).
