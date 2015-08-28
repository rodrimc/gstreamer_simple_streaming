#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>


typedef struct _ServerData 
{
  GMainLoop *loop;
  GstElement *pipeline;
  GstElement *source;
  GstElement *a_filter;  //Format conversion
  GstElement *a_enc_buffer;
  GstElement *a_encoder;
  GstElement *v_enc_buffer;
  GstElement *v_encoder;
  GstElement *muxer;
  GstElement *sink_buffer;
  GstElement *sink;
} ServerData;

//Command line args
static char *addr     = NULL;
static gchar *net_if  = NULL;
static gchar *uri     = NULL;
static gint port      = -1;

//Function Declarations
static int init (ServerData *);
static int set_links (ServerData *); 
static int check_args (void);
static void pad_added (GstElement *, GstPad *, ServerData *); 
static gboolean bus_call (GstBus *, GstMessage*, gpointer);
static int link_encoders_muxer (ServerData *app);
static char *if_addr (char *);
static gboolean handle_input (GIOChannel *, GIOCondition, gpointer);


//Function Definitions
static gboolean handle_input (GIOChannel *io_channel, GIOCondition cond, 
    gpointer data)
{
  ServerData *app = (ServerData *) data;
  GError *error = NULL;
  gchar in;

  switch (g_io_channel_read_chars (io_channel, &in, 1, NULL, &error)) 
  {
    case G_IO_STATUS_NORMAL:
      if ('q' == in) 
      {
        fprintf (stdout, "Quitting...\n");
        g_main_loop_quit (app->loop);
        return FALSE;
      } 
      return TRUE;

    case G_IO_STATUS_ERROR:
      fprintf (stderr, "IO error: %s\n", error->message);
      g_error_free (error);

      return FALSE;
    case G_IO_STATUS_EOF:
      g_warning ("No input data available");
      return TRUE;

    default:
      return TRUE;
  }
}

static int init (ServerData *app)
{
  int error_flag = 0;
  gst_init (NULL, NULL);

  app->loop         = g_main_loop_new (NULL, FALSE);
  app->pipeline     = gst_pipeline_new ("server");
  app->source       = gst_element_factory_make ("uridecodebin", "decoder");
  app->a_filter     = gst_element_factory_make ("audioconvert","audio filter");
  app->a_enc_buffer = gst_element_factory_make ("queue", "audio enc buffer");
  app->a_encoder    = gst_element_factory_make ("vorbisenc", "audio enconder");
  app->v_enc_buffer = gst_element_factory_make ("queue", "video enc buffer");
  app->v_encoder    = gst_element_factory_make ("jpegenc", "video encoder");
  app->muxer        = gst_element_factory_make ("matroskamux", "muxer");
  app->sink_buffer  = gst_element_factory_make ("queue", "sink buffer");
  app->sink         = gst_element_factory_make ("tcpserversink", "sink");

  if (app->pipeline == NULL || app->source == NULL || app->a_filter == NULL ||
      app->a_enc_buffer == NULL || app->a_encoder == NULL || 
      app->v_enc_buffer == NULL || app->v_encoder == NULL || 
      app->muxer == NULL ||  app->sink_buffer == NULL  || app->sink == NULL)
  {
    fprintf (stderr, "Error while instantiating elements\n");
    error_flag = 1;
  }
  else
  {
    GIOChannel *io = NULL;

    io = g_io_channel_unix_new (STDIN_FILENO);
    g_io_add_watch (io, G_IO_IN, handle_input, app);
    g_io_channel_unref (io);

    gst_bin_add_many (GST_BIN (app->pipeline), app->source, app->a_filter, 
        app->a_enc_buffer, app->a_encoder, app->v_enc_buffer, app->v_encoder, 
        app->muxer, app->sink_buffer, app->sink, 
        NULL);
  }

  return error_flag;
}

static int set_links (ServerData *app)
{
  GstCaps *a_caps = NULL;
  int error_flag = 0;
  
  if (!gst_element_link_many (app->muxer, app->sink_buffer, app->sink, NULL))
  {
    fprintf (stderr, "Could not link elements: muxer --> buffer --> sink\n");
    error_flag = 1;
  }

  if (!gst_element_link (app->a_enc_buffer, app->a_encoder))
  {
    fprintf (stderr, "Could not link elements: queue --> audio encoder\n");
    error_flag = 1;
  }

  if (!gst_element_link (app->v_enc_buffer, app->v_encoder))
  {
    fprintf (stderr, "Could not link elements: queue --> video encoder\n");
    error_flag = 1;
  }

  error_flag = link_encoders_muxer (app);
  
  a_caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "F32LE",
      "rate", G_TYPE_INT, 48000,
      NULL);

  if (!gst_element_link_filtered (app->a_filter, app->a_enc_buffer, a_caps))
  {
    fprintf (stderr, "Could not link audio filter and audio enc buffer\n");
    error_flag = 1;
  }

  return error_flag;
}

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  ServerData *app = (ServerData *) data;
  switch (GST_MESSAGE_TYPE (msg))
  {
    case GST_MESSAGE_EOS:
    {
      fprintf (stdout, "end-of-stream\n");
    
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

static void pad_added (GstElement *src, GstPad *new_pad, ServerData *app)
{
  GstElement *parent_pad = NULL;
  GstPad *sink_pad = NULL;
  GstPadLinkReturn ret;
  GstCaps *caps = NULL;
  GstStructure *pad_struct = NULL;
  gchar *src_pad_name = NULL, *sink_pad_name = NULL;
  const gchar *struct_name = NULL;
  
  caps = gst_pad_query_caps (new_pad, NULL); 
  pad_struct = gst_caps_get_structure (caps, 0);
  struct_name = gst_structure_get_name (pad_struct);

  g_debug ("Pad structure: %s\n", struct_name);

  if (strcmp (struct_name, "video/x-raw") == 0)
    sink_pad = gst_element_get_static_pad (app->v_enc_buffer, "sink");
  else if (strcmp (struct_name, "audio/x-raw") == 0)
    sink_pad = gst_element_get_static_pad (app->a_filter, "sink");
  else
    fprintf (stderr, "Cannot handle this stream: %s\n", struct_name);

  gst_caps_unref (caps);

  if (sink_pad == NULL)
    g_debug ("Could not get a pad from the encoder/muxer\n");
  else
  {
    src_pad_name = gst_pad_get_name (new_pad);
    sink_pad_name = gst_pad_get_name (sink_pad);
    parent_pad = gst_pad_get_parent_element (sink_pad);

    g_debug ("Trying to link pads:  %s[%s] --> %s[%s]: ",
        GST_ELEMENT_NAME (src), src_pad_name, 
        GST_ELEMENT_NAME (parent_pad), sink_pad_name);

    gst_object_unref (parent_pad);
    g_free (src_pad_name);
    g_free (sink_pad_name);

    ret = gst_pad_link (new_pad, sink_pad);
    if (ret != GST_PAD_LINK_OK)
    {
      g_debug ("Could not link pads (return = %d)\n", ret);
      fprintf (stderr, "Internal pipeline error\n");
    }
    else
      g_debug ("Pads linked\n");

    gst_object_unref (sink_pad);
  }       
}

static int link_encoders_muxer (ServerData *app)
{
  GstPad *v_src_pad = NULL, *v_mux_pad = NULL,
         *a_src_pad = NULL, *a_mux_pad = NULL;
  int error = 0;

  v_src_pad = gst_element_get_static_pad (app->v_encoder, "src");
  v_mux_pad = gst_element_get_request_pad (app->muxer, "video_%u");

  a_src_pad = gst_element_get_static_pad (app->a_encoder, "src");
  a_mux_pad = gst_element_get_request_pad (app->muxer, "audio_%u");

  if (a_src_pad == NULL)
  {
    fprintf (stderr, "Could not get a pad from audio encoder\n");
    error = 1;
  }

  if (a_mux_pad == NULL)
  {
    fprintf (stderr, "Could not get an audio pad from muxer\n");
    error = 1;
  }

  if (v_src_pad == NULL)
  {
    fprintf (stderr, "Could not get a pad from video encoder\n");
    error = 1;
  }

  if (v_mux_pad == NULL)
  {
    fprintf (stderr, "Could not get a video pad from muxer\n");
    error = 1;
  }

  if (!error)
  {
    g_debug ("Trying to link video encoder and muxer: ");
    if (gst_pad_link (v_src_pad, v_mux_pad) == GST_PAD_LINK_OK)
      g_debug ("succeed\n");
    else
    {
      g_debug ("failed\n"); 
      error = 1;
    }

    g_debug ("Trying to link audio encoder and muxer: ");
    if (gst_pad_link (a_src_pad, a_mux_pad) == GST_PAD_LINK_OK)
      g_debug ("succeed\n");
    else
    {
      g_debug ("failed\n"); 
      error = 1;
    }
  }

  gst_object_unref (v_src_pad);
  gst_object_unref (v_mux_pad);
  gst_object_unref (a_src_pad);
  gst_object_unref (a_mux_pad);

  return error;
}

static char *if_addr (char *interface)
{
  char *addr;
  int fd;
  struct ifreq ifr;
  
  fd = socket (AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
  {
    fprintf (stderr, "Could not open a socket\n");
    return NULL;
  }

  strncpy (ifr.ifr_name, interface, IFNAMSIZ-1);

  if (ioctl (fd, SIOCGIFADDR, &ifr) < 0)
  {
    fprintf (stderr, "ioctl error\n");
    close (fd);
    return NULL;
  }
  
  addr = (char *) calloc (16, sizeof (char));
  strcpy (addr, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

  return  addr;
}

int check_args ()
{
  int error_flag = 0;
  if (net_if == NULL)
  {
    fprintf (stderr, "Error: unknown network interface\n");
    error_flag = 1;
  }

  if (uri == NULL)
  {
    fprintf (stderr, "Error: URI unspecified\n");
    error_flag = 1;
  }

  if (port == -1)
  {
    fprintf (stderr, "Error: port unspecified\n");
    error_flag = 1;
  }

  return error_flag;
}

int main (int argc, char *argv[])
{
  ServerData app;
  GError *error           = NULL;
  GOptionContext *context = NULL;
  GstBus *bus             = NULL;
  int error_flag          = 0;

  GOptionEntry entries [] =
  {
    { "interface", 'i', 0, G_OPTION_ARG_STRING, &net_if, 
      "network interface", NULL},
    { "port", 'p', 0, G_OPTION_ARG_INT, &port, 
      "port number", NULL },
    { "fileuri", 'f', 0, G_OPTION_ARG_STRING, &uri, 
      "URI to stream", "URI (<protocol>://<location>)"},
    { NULL }
  };

  context = g_option_context_new ("- simple tcp streaming server");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
  {
    fprintf (stderr, "option parsing failed: %s\n", error->message);
    error_flag = 1;
  }
  
  error_flag = check_args ();

  if (error_flag)
  {
    fprintf (stderr, "Exiting...\n");
    return EXIT_FAILURE;
  }

  addr = if_addr (net_if);
  if (addr == NULL)
  {
    fprintf (stderr, "There is no ip address bound to the interface %s\n", 
        net_if);
    return EXIT_FAILURE;
  }
  
  error_flag =  init (&app);
  if (!error_flag)
    error_flag = set_links (&app);
  
  if (error_flag)
  {
    fprintf (stderr, "Exiting...\n");
    return EXIT_FAILURE;
  }

  g_object_set (GST_OBJECT (app.source), "uri", uri, NULL);
  g_object_set (GST_OBJECT (app.muxer), "streamable", TRUE, NULL);
  g_object_set (GST_OBJECT (app.sink), "host", addr, NULL); 
  g_object_set (GST_OBJECT (app.sink), "port", port, NULL); 

  g_signal_connect (app.source, "pad-added", G_CALLBACK (pad_added), &app);
  
  
  bus = gst_pipeline_get_bus (GST_PIPELINE (app.pipeline));
  gst_bus_add_watch (bus, bus_call, &app);

  fprintf (stdout, "Preparing the streaming of: %s\n"
      "Network Interface: %s\n"
      "Server IP: %s\n"
      "Server port: %d\n", uri, net_if, addr, port);
  
  fprintf (stdout, "Press 'q' to stop streaming and quit\n");
  free (addr);

  gst_element_set_state (GST_ELEMENT (app.pipeline), GST_STATE_PLAYING);
  g_main_loop_run (app.loop);
  
  gst_element_set_state (GST_ELEMENT (app.pipeline), GST_STATE_NULL);
  
  gst_object_unref (GST_OBJECT(bus));
  gst_object_unref (GST_OBJECT (app.pipeline));

  fprintf (stdout, "All done\n");
  return EXIT_SUCCESS;
}
