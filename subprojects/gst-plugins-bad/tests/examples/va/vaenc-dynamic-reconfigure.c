/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
 *               2022 Víctor Jáquez <vjaquez@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdlib.h>
#include "../key-handler.h"

static GMainLoop *loop = NULL;
static gint width = 640;
static gint height = 480;
static guint rc_ctrl = 0;

G_LOCK_DEFINE_STATIC (input_lock);

typedef struct
{
  GstElement *pipeline;
  GstElement *capsfilter;
  GstElement *encoder;
  gulong probe_id;

  gint prev_width;
  gint prev_height;
} TestCallbackData;

static gboolean
check_encoder_available (const gchar * encoder_name)
{
  gboolean ret = TRUE;
  GstElement *elem;

  elem = gst_element_factory_make (encoder_name, NULL);
  if (!elem) {
    gst_printerrln ("%s is not available", encoder_name);
    return FALSE;
  }

  if (gst_element_set_state (elem,
          GST_STATE_PAUSED) != GST_STATE_CHANGE_SUCCESS) {
    gst_printerrln ("cannot open device");
    ret = FALSE;
  }

  gst_element_set_state (elem, GST_STATE_NULL);
  gst_object_unref (elem);

  return ret;
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      gst_printerrln ("ERROR %s", err->message);
      if (dbg != NULL)
        gst_printerrln ("ERROR debug information: %s", dbg);
      g_clear_error (&err);
      g_free (dbg);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_PROPERTY_NOTIFY:{
      const GValue *val;
      const gchar *name;
      GstObject *obj;
      gchar *val_str = NULL;
      gchar *obj_name;

      gst_message_parse_property_notify (msg, &obj, &name, &val);

      if (!GST_IS_VIDEO_ENCODER (obj))
        break;

      obj_name = gst_object_get_name (GST_OBJECT (obj));
      if (val) {
        if (G_VALUE_HOLDS_STRING (val))
          val_str = g_value_dup_string (val);
        else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
          val_str = gst_caps_to_string (g_value_get_boxed (val));
        else if (G_VALUE_HOLDS_BOOLEAN (val) || G_VALUE_HOLDS_INT (val)
            || G_VALUE_HOLDS_UINT (val) || G_VALUE_HOLDS_ENUM (val))
          val_str = gst_value_serialize (val);
        else
          val_str = g_strdup ("(unknown type)");
      } else {
        val_str = g_strdup ("(no value)");
      }

      gst_println ("%s: %s = %s", obj_name, name, val_str);
      g_free (obj_name);
      g_free (val_str);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
loop_rate_control (GstElement * encoder)
{
  GParamSpec *pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (encoder),
      "rate-control");
  GEnumClass *enum_class;
  gint i, default_value;

  if (!pspec)
    return;

  enum_class = G_PARAM_SPEC_ENUM (pspec)->enum_class;

  if (rc_ctrl == 0) {
    default_value = G_PARAM_SPEC_ENUM (pspec)->default_value;
    for (i = 0; i < enum_class->n_values; i++) {
      if (enum_class->values[i].value == default_value) {
        rc_ctrl = i;
        break;
      }
    }
  }

  i = ++rc_ctrl % enum_class->n_values;
  g_object_set (encoder, "rate-control", enum_class->values[i].value, NULL);
}

static GstPadProbeReturn
resolution_change_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  TestCallbackData *data = (TestCallbackData *) user_data;

  G_LOCK (input_lock);

  if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    GstPad *peer = gst_pad_get_peer (pad);
    GstFlowReturn flow_ret = GST_FLOW_OK;

    ret = GST_PAD_PROBE_HANDLED;

    if (peer) {
      flow_ret = gst_pad_chain (peer, buffer);

      if (flow_ret != GST_FLOW_OK) {
        gst_pad_remove_probe (pad, data->probe_id);
        data->probe_id = 0;
      } else {
        if (data->prev_width != width || data->prev_height != height) {
          GstCaps *caps = NULL;
          gint next_width, next_height;

          next_width = width;
          next_height = height;

          g_object_get (data->capsfilter, "caps", &caps, NULL);
          caps = gst_caps_make_writable (caps);
          gst_caps_set_simple (caps,
              "width", G_TYPE_INT, next_width, "height", G_TYPE_INT,
              next_height, NULL);
          g_object_set (data->capsfilter, "caps", caps, NULL);
          gst_caps_unref (caps);

          data->prev_width = next_width;
          data->prev_height = next_height;
        }
      }
    }
  }

  G_UNLOCK (input_lock);

  return ret;
}

static void
print_keyboard_help (void)
{
  /* *INDENT-OFF* */
  static struct
  {
    const gchar *key_desc;
    const gchar *key_help;
  } key_controls[] = {
    {
    "q", "Quit"}, {
    "right arrow", "Increase Width"}, {
    "left arrow", "Decrease Width"}, {
    "up arrow", "Increase Height"}, {
    "down arrow", "Decrease Height"}, {
    "r", "Loop rate control"}, {
    ">", "Increase bitrate by 100 kbps"}, {
    "<", "Decrease bitrate by 100 kbps"}, {
    "]", "Increase target usage"}, {
    "[", "Decrease target usage"}, {
    "}", "Increase target percentage by 10% (only in VBR)"}, {
    "{", "Decrease target percentage by 10% (only in VBR)"}, {
    "I", "Increase QP-I"}, {
    "i", "Decrease QP-I"}, {
    "P", "Increase QP-P (only in CQP)"}, {
    "p", "Decrease QP-P (only in CQP)"}, {
    "B", "Increase QP-B (only in CQP)"}, {
    "b", "Decrease QP-B (only in CQP)"}, {
    "k", "show keyboard shortcuts"}
  };
  /* *INDENT-ON* */

  guint i, chars_to_pad, desc_len, max_desc_len = 0;

  gst_print ("\n\n%s\n\n", "Keyboard controls:");

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    desc_len = g_utf8_strlen (key_controls[i].key_desc, -1);
    max_desc_len = MAX (max_desc_len, desc_len);
  }
  ++max_desc_len;

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    chars_to_pad = max_desc_len - g_utf8_strlen (key_controls[i].key_desc, -1);
    gst_print ("\t%s", key_controls[i].key_desc);
    gst_print ("%-*s: ", chars_to_pad, "");
    gst_print ("%s\n", key_controls[i].key_help);
  }
  gst_print ("\n");
}

static inline gboolean
is_ratectl (GstElement * encoder, guint rc)
{
  guint ratectl = 0;

  g_object_get (encoder, "rate-control", &ratectl, NULL);
  return (ratectl == rc);
}

static void
keyboard_cb (gchar input, gboolean is_ascii, gpointer user_data)
{
  TestCallbackData *data = (TestCallbackData *) user_data;

  G_LOCK (input_lock);

  if (!is_ascii) {
    switch (input) {
      case KB_ARROW_UP:
        height += 2;
        break;
      case KB_ARROW_DOWN:
        height -= 2;
        height = MAX (height, 16);
        break;
      case KB_ARROW_LEFT:
        width -= 2;
        width = MAX (width, 16);
        break;
      case KB_ARROW_RIGHT:
        width += 2;
        break;
      default:
        break;
    }
  } else {
    switch (input) {
      case 'k':
      case 'K':
        print_keyboard_help ();
        break;
      case 'q':
      case 'Q':
        gst_element_send_event (data->pipeline, gst_event_new_eos ());
        g_main_loop_quit (loop);
        break;
      case 'r':
      case 'R':
        loop_rate_control (data->encoder);
        break;
      case '>':{
        guint bitrate;

        if (is_ratectl (data->encoder, 0x00000010 /* VA_RC_CQP */ ))
          break;

        g_object_get (data->encoder, "bitrate", &bitrate, NULL);
        bitrate += 100;
        if (bitrate <= 2048000)
          g_object_set (data->encoder, "bitrate", bitrate, NULL);
        break;
      }
      case '<':{
        gint bitrate;

        if (is_ratectl (data->encoder, 0x00000010 /* VA_RC_CQP */ ))
          break;

        g_object_get (data->encoder, "bitrate", &bitrate, NULL);
        bitrate -= 100;
        if (bitrate < 0)
          bitrate = 0;
        g_object_set (data->encoder, "bitrate", bitrate, NULL);
        break;
      }
      case ']':{
        guint usage;

        g_object_get (data->encoder, "target-usage", &usage, NULL);
        usage += 1;
        if (usage <= 7)
          g_object_set (data->encoder, "target-usage", usage, NULL);
        break;
      }
      case '[':{
        guint usage;

        g_object_get (data->encoder, "target-usage", &usage, NULL);
        usage -= 1;
        if (usage >= 1)
          g_object_set (data->encoder, "target-usage", usage, NULL);
        break;
      }
      case '}':{
        guint target;

        if (!is_ratectl (data->encoder, 0x00000004 /* VA_RC_VBR */ ))
          break;

        g_object_get (data->encoder, "target-percentage", &target, NULL);
        target += 10;
        if (target <= 100)
          g_object_set (data->encoder, "target-percentage", target, NULL);
        break;
      }
      case '{':{
        guint target;

        if (!is_ratectl (data->encoder, 0x00000004 /* VA_RC_VBR */ ))
          break;

        g_object_get (data->encoder, "target-percentage", &target, NULL);
        target -= 10;
        if (target >= 50)
          g_object_set (data->encoder, "target-percentage", target, NULL);
        break;
      }
      case 'I':{
        guint qpi;

        g_object_get (data->encoder, "qpi", &qpi, NULL);
        qpi += 1;
        if (qpi <= 51)
          g_object_set (data->encoder, "qpi", qpi, NULL);
        break;
      }
      case 'i':{
        gint qpi;

        g_object_get (data->encoder, "qpi", &qpi, NULL);
        qpi -= 1;
        if (qpi >= 0)
          g_object_set (data->encoder, "qpi", qpi, NULL);
        break;
      }
      case 'P':{
        guint qpp;

        if (!is_ratectl (data->encoder, 0x00000010 /* VA_RC_CQP */ ))
          break;

        g_object_get (data->encoder, "qpp", &qpp, NULL);
        qpp += 1;
        if (qpp <= 51)
          g_object_set (data->encoder, "qpp", qpp, NULL);
        break;
      }
      case 'p':{
        gint qpp;

        if (!is_ratectl (data->encoder, 0x00000010 /* VA_RC_CQP */ ))
          break;

        g_object_get (data->encoder, "qpp", &qpp, NULL);
        qpp -= 1;
        if (qpp >= 0)
          g_object_set (data->encoder, "qpp", qpp, NULL);
        break;
      }
      case 'B':{
        guint qpb;

        if (!is_ratectl (data->encoder, 0x00000010 /* VA_RC_CQP */ ))
          break;

        g_object_get (data->encoder, "qpb", &qpb, NULL);
        qpb += 1;
        if (qpb <= 51)
          g_object_set (data->encoder, "qpb", qpb, NULL);
        break;
      }
      case 'b':{
        gint qpb;

        if (!is_ratectl (data->encoder, 0x00000010 /* VA_RC_CQP */ ))
          break;

        g_object_get (data->encoder, "qpb", &qpb, NULL);
        qpb -= 1;
        if (qpb >= 0)
          g_object_set (data->encoder, "qpb", qpb, NULL);
        break;
      }
      default:
        break;
    }
  }

  G_UNLOCK (input_lock);
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline;
  GstElement *src, *capsfilter, *enc, *dec, *parser, *sink;
  GstStateChangeReturn sret;
  GError *error = NULL;
  GOptionContext *option_ctx;
  GstCaps *caps;
  GstPad *pad;
  TestCallbackData data = { 0, };
  gchar *encoder_name = NULL;
  gulong deep_notify_id = 0;

  /* *INDENT-OFF* */
  GOptionEntry options[] = {
    {"encoder", 0, 0, G_OPTION_ARG_STRING, &encoder_name,
        "VA video encoder element to test, default: vah264enc"},
    {NULL}
  };
  /* *INDENT-ON* */

#define MAKE_ELEMENT_AND_ADD(elem, name) G_STMT_START { \
  GstElement *_elem = gst_element_factory_make (name, NULL); \
  if (!_elem) { \
    gst_printerrln ("%s is not available", name); \
    exit (1); \
  } \
  gst_println ("Adding element %s", name); \
  elem = _elem; \
  gst_bin_add (GST_BIN (pipeline), elem); \
} G_STMT_END

  option_ctx =
      g_option_context_new ("VA video encoder dynamic reconfigure example");
  g_option_context_add_main_entries (option_ctx, options, NULL);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());
  g_option_context_set_help_enabled (option_ctx, TRUE);
  if (!g_option_context_parse (option_ctx, &argc, &argv, &error)) {
    gst_printerrln ("option parsing failed: %s\n", error->message);
    g_clear_error (&error);
    exit (1);
  }

  g_option_context_free (option_ctx);
  gst_init (NULL, NULL);

  if (!encoder_name)
    encoder_name = g_strdup ("vah264enc");

  if (!check_encoder_available (encoder_name)) {
    gst_printerrln ("Cannot load %s plugin", encoder_name);
    exit (1);
  }

  /* prepare the pipeline */
  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new (NULL);

  MAKE_ELEMENT_AND_ADD (src, "videotestsrc");
  g_object_set (src, "pattern", 1, NULL);

  MAKE_ELEMENT_AND_ADD (capsfilter, "capsfilter");
  MAKE_ELEMENT_AND_ADD (enc, encoder_name);

  /* gst_util_set_object_arg (G_OBJECT (enc), "rate-control", rate_control); */

  if (g_strcmp0 (encoder_name, "vah264enc") == 0) {
    MAKE_ELEMENT_AND_ADD (parser, "h264parse");
    MAKE_ELEMENT_AND_ADD (dec, "vah264dec");
  } else {
    g_assert_not_reached ();
  }

  MAKE_ELEMENT_AND_ADD (sink, "autovideosink");

  if (!gst_element_link_many (src, capsfilter, enc, parser, dec, sink, NULL)) {
    gst_printerrln ("Failed to link element");
    exit (1);
  }

  caps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
      width, "height", G_TYPE_INT, height, NULL);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  data.pipeline = pipeline;
  data.capsfilter = capsfilter;
  data.encoder = enc;

  pad = gst_element_get_static_pad (capsfilter, "src");
  data.probe_id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      resolution_change_probe, &data, NULL);
  gst_object_unref (pad);
  data.prev_width = width;
  data.prev_height = height;

  deep_notify_id =
      gst_element_add_property_deep_notify_watch (pipeline, NULL, TRUE);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_msg, &data);

  /* run the pipeline */
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (sret == GST_STATE_CHANGE_FAILURE) {
    gst_printerrln ("Pipeline doesn't want to playing\n");
  } else {
    set_key_handler ((KeyInputCallback) keyboard_cb, &data);
    g_main_loop_run (loop);
    unset_key_handler ();
  }

  if (deep_notify_id != 0)
    g_signal_handler_disconnect (pipeline, deep_notify_id);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (pipeline));

  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
  g_free (encoder_name);

  return 0;
}
