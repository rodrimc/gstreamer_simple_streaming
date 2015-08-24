#GStreamer Simple Streaming
This is a simple source code that provides an implementation of a streaming client using GStreamer. The client connects to a TCP streaming server.

#How to use
For running the client, it should be specified the server address and port:

./tcp_client -s server_address -p port

To test the client, it is possible to use the gst-launch tool to build a pipeline that streams a video/audio.
A simple example is:

gst-launch-1.0 uridecodebin uri=\<uri\> ! jpegenc ! avimux ! tcpserversink port=8554


#Dependencies
This code depends of [GStreamer 1.x](http://gstreamer.freedesktop.org/ "GStreamer") and [GLib 2.0](https://developer.gnome.org/glib/).
