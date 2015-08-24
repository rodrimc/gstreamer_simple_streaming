#include <gst/gst.h>

typedef struct _ClientData {
  GMainLoop *loop;
  GstElement *pipeline;
  GstElement *source;
  GstElement *buffer;
  GstElement *decoder;
  GstElement *audio_sink;
  GstElement *video_sink;
} ClientData;

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  ClientData *app = (ClientData *) data;
  switch (GST_MESSAGE_TYPE (msg))
  {
    case GST_MESSAGE_EOS:
    {
      fprintf (stderr, "end-of-stream\n");
     
      g_main_loop_quit (app->loop);

      break;
    }
    case GST_MESSAGE_ERROR:
    { 
      GError *err;
      gst_message_parse_error (msg, &err, NULL);
      fprintf (stderr, "Error (%s): %s\n", GST_OBJECT_NAME (msg->src),
          err->message);

      g_error_free (err);

      g_main_loop_quit (app->loop);

      break;
    }
    default:
      break;
  }

  return TRUE;
}
