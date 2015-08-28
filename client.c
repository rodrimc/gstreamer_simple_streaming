#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _ClientData {
  GMainLoop *loop;
  GstElement *pipeline;
  GstElement *source;
  GstElement *buffer;
  GstElement *decoder;
  GstElement *audio_sink;
  GstElement *video_sink;
} ClientData;

static void pad_added (GstElement *, GstPad *, ClientData *); 
static gboolean bus_call (GstBus *, GstMessage *, gpointer);

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

static gchar* server = NULL;
static gint port     = -1;

static GOptionEntry entries [] =
{
  { "server", 's', 0, G_OPTION_ARG_STRING, &server, "server address", NULL},
  { "port", 'p', 0, G_OPTION_ARG_INT, &port, "server port number",NULL},
  { NULL }
};

static void pad_added (GstElement *src, GstPad *new_pad, ClientData *app)
{
  GstPad *sink_pad = NULL;
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  GstElement *sink = NULL;
  gchar *pad_name = NULL;
  const gchar *new_pad_type = NULL;

  pad_name = gst_pad_get_name (new_pad);
  
  fprintf (stdout, "Pad name: %s (%s)\n", pad_name, GST_ELEMENT_NAME (src));
  
  new_pad_caps = gst_pad_query_caps (new_pad, NULL);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0); 
  new_pad_type = gst_structure_get_name (new_pad_struct);
  

  if (g_str_has_prefix (new_pad_type, "audio"))
  {
    if (app->audio_sink == NULL)
    {
      app->audio_sink  = gst_element_factory_make ("autoaudiosink", "audio_sink");
      sink = app->audio_sink;
    }
  }
  else
  {
    if (app->video_sink == NULL)
    {
      app->video_sink  = gst_element_factory_make ("xvimagesink", 
          "video_sink");
      sink = app->video_sink;
    }
  }
 
  if (sink == NULL)
  {
    fprintf (stderr, "Could not create sink element.\nExiting...");
    exit (0);
  }

  gst_bin_add (GST_BIN (app->pipeline), sink);
  gst_element_set_state (GST_ELEMENT (sink), GST_STATE_PLAYING);
    
  sink_pad = gst_element_get_static_pad (sink, "sink");

  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret))
  {
    fprintf (stderr, "   New pad link has failed");
  }
  else
    fprintf (stderr, "   New pad has been linked");

  fprintf (stderr, " (%s --> %s).\n", GST_ELEMENT_NAME (src), 
      GST_ELEMENT_NAME(gst_pad_get_parent_element(sink_pad)));

  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  gst_object_unref (sink_pad);   
  g_free (pad_name);
}

int main (int argc, char *argv[])
{
  GError *error = NULL;
  GOptionContext *context = NULL;
  int error_flag = 0;

  ClientData app;
  GstBus *bus = NULL;

  context = g_option_context_new ("- simple tcp streaming client");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
  {
    fprintf (stderr, "option parsing failed: %s\n", error->message);
    exit (1);
  }

  if (server == NULL)
  {
    printf ("Error: unknown server\n");
    error_flag = 1;
  }

  if (port == -1)
  {
    fprintf (stderr, "Error: server port unspecified\n");
    error_flag = 1;
  }

  if (error_flag)
  {
    fprintf (stderr, "Exiting...\n");
    return 0;
  }

  gst_init (NULL, NULL);

  app.loop        = g_main_loop_new (NULL, FALSE);
  app.pipeline    = gst_pipeline_new ("client");
  app.source      = gst_element_factory_make ("tcpclientsrc", "source");
  app.buffer      = gst_element_factory_make ("queue2", "buffer");
  app.decoder     = gst_element_factory_make ("decodebin", "decoder");

  /* Sinks will be instantiated as soon as the dynamic pads has been created */
  app.audio_sink  = NULL;
  app.video_sink  = NULL;

  if (app.pipeline == NULL || app.source == NULL || app.buffer == NULL
      || app.decoder == NULL)
  {
    fprintf (stderr, "Error while instantiating elements.\nExiting...");
    return 0;
  }
 
  g_print ("Pipeline created successfully.\n");

  gst_bin_add_many (GST_BIN (app.pipeline), app.source, app.buffer, 
      app.decoder, NULL);
  
  if (!gst_element_link_many (app.source, app.buffer, app.decoder, NULL))
  {
    fprintf (stderr, "Could not link elements: source --> buffer --> decoder\n"
          "Exiting...\n");
    return 0;
  }

  g_object_set (G_OBJECT (app.source), 
      "host", server, 
      "port", port,
      NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (app.pipeline));
  gst_bus_add_watch (bus, bus_call, &app);

  g_signal_connect (app.decoder, "pad-added", G_CALLBACK (pad_added), &app);

  gst_element_set_state (GST_ELEMENT (app.pipeline), GST_STATE_PLAYING);
  g_main_loop_run (app.loop);

  gst_element_set_state (GST_ELEMENT (app.pipeline), GST_STATE_NULL);
  gst_object_unref (GST_OBJECT(bus));
  gst_object_unref (GST_OBJECT(app.pipeline));
  
  fprintf (stdout, "All done\n");
  return 0;
}
