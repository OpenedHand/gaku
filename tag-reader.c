/*
 * Copyright (C) 2006 OpenedHand Ltd.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Jorn Baayen <jorn@openedhand.com>
 */

#include <gst/gst.h>
#include <string.h>

#include "marshal.h"

#include "tag-reader.h"

G_DEFINE_TYPE (TagReader,
               tag_reader,
               G_TYPE_OBJECT);

struct _TagReaderPrivate {
        GstElement *pipeline;
        GstElement *src;
        GstElement *decodebin;
        GstElement *sink;

        GQueue *queue;

        guint next_id;

        GError *current_error;
        GstTagList *current_tag_list;
};

enum {
        SIGNAL_URI_SCANNED,
        SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

typedef struct {
        char *uri;
        guint id;
} ScanUriData;

static void
scan_uri_data_free (ScanUriData *data)
{
        g_free (data->uri);

        g_slice_free (ScanUriData, data);
}

/**
 * Feed the head of the queue to the pipeline.
 **/
static void
feed_head (TagReader *tag_reader)
{
        ScanUriData *data;

        data = g_queue_peek_head (tag_reader->priv->queue);
        if (!data)
                return;

        /**
         * Get appropriate src element.
         **/
        tag_reader->priv->src =
                gst_element_make_from_uri (GST_URI_SRC, data->uri, "src");
        
        /**
         * Add to pipeline & link up.
         **/
        gst_bin_add (GST_BIN (tag_reader->priv->pipeline),
                     tag_reader->priv->src);
        gst_element_link (tag_reader->priv->src, tag_reader->priv->decodebin);
        
        /**
         * Play pipeline.
         **/
        gst_element_set_state (tag_reader->priv->pipeline,
                               GST_STATE_PLAYING);
}

/**
 * Purge the head of the queue.
 **/
static void
flush_head (TagReader *tag_reader)
{
        ScanUriData *data;
        
        /**
         * Stop pipeline.
         **/
        gst_element_set_state (tag_reader->priv->pipeline,
                               GST_STATE_NULL);

        /**
         * Remove source element.
         **/
        gst_element_unlink (tag_reader->priv->src, tag_reader->priv->decodebin);
        gst_bin_remove (GST_BIN (tag_reader->priv->pipeline),
                        tag_reader->priv->src);

        /**
         * Pop head from queue.
         **/
        data = g_queue_pop_head (tag_reader->priv->queue);

        /**
         * Call callback.
         **/
        g_signal_emit (tag_reader,
                       signals[SIGNAL_URI_SCANNED],
                       0,
                       data->uri,
                       tag_reader->priv->current_error,
                       tag_reader->priv->current_tag_list);

        /**
         * Free data.
         **/
        if (tag_reader->priv->current_error) {
                g_error_free (tag_reader->priv->current_error);
                tag_reader->priv->current_error = NULL;
        }
        
        if (tag_reader->priv->current_tag_list) {
                gst_tag_list_free (tag_reader->priv->current_tag_list);
                tag_reader->priv->current_tag_list = NULL;
        }

        scan_uri_data_free (data);
}

/**
 * An error occured.
 **/
static void
bus_message_error_cb (GstBus     *bus,
                      GstMessage *message,
                      TagReader  *tag_reader)
{
        ScanUriData *data;

        data = g_queue_peek_head (tag_reader->priv->queue);

        gst_message_parse_error (message,
                                 &tag_reader->priv->current_error,
                                 NULL);

        flush_head (tag_reader);
        feed_head (tag_reader);
}

/**
 * End of stream reached.
 **/
static void
bus_message_eos_cb (GstBus     *bus,
                    GstMessage *message,
                    TagReader  *tag_reader)
{
        flush_head (tag_reader);
        feed_head (tag_reader);
}

/**
 * Tag list available.
 **/
static void
bus_message_tag_cb (GstBus     *bus,
                    GstMessage *message,
                    TagReader  *tag_reader)
{
        GstTagList *tags, *new_tags;

        gst_message_parse_tag (message, &tags);

        new_tags = gst_tag_list_merge (tag_reader->priv->current_tag_list, tags, GST_TAG_MERGE_REPLACE);

        if (tag_reader->priv->current_tag_list)
                gst_tag_list_free (tag_reader->priv->current_tag_list);
        gst_tag_list_free (tags);

        tag_reader->priv->current_tag_list = new_tags;
}

/**
 * Application message received.
 **/
static void
bus_message_application_cb (GstBus     *bus,
                            GstMessage *message,
                            TagReader  *tag_reader)
{
        gpointer src;
        const GstStructure *structure;
        const char *structure_name;
        ScanUriData *data;
        GstQuery *query;

        /**
         * Verify this is the fakesink handoff event.
         **/
        src = GST_MESSAGE_SRC (message);
        if (src != tag_reader->priv->sink)
                return;
        
        structure = gst_message_get_structure (message);
        structure_name = gst_structure_get_name (structure);
        if (strcmp (structure_name, "handoff"))
                return;

        /**
         * Get relevant ScanUriData.
         **/
        data = g_queue_peek_head (tag_reader->priv->queue);

        /**
         * Determine the duration.
         **/
        query = gst_query_new_duration (GST_FORMAT_TIME);

        if (gst_element_query (tag_reader->priv->pipeline, query)) {
                gint64 duration;

                gst_query_parse_duration (query,
                                          NULL,
                                          &duration);

                /**
                 * Create tag list if none exists yet.
                 **/
                if (!tag_reader->priv->current_tag_list) {
                        tag_reader->priv->current_tag_list =
                                gst_tag_list_new ();
                }

                /**
                 * Merge duration info into tag list.
                 **/
                gst_tag_list_add (tag_reader->priv->current_tag_list,
                                  GST_TAG_MERGE_REPLACE,
                                  GST_TAG_DURATION,
                                  duration,
                                  NULL);
        }

        gst_query_unref (query);

        /**
         * Next, please.
         **/
        flush_head (tag_reader);
        feed_head (tag_reader);
}

/**
 * New decoded pad: Hook up to fakesink.
 **/
static void
decodebin_new_decoded_pad_cb (GstElement *decodebin,
                              GstPad     *pad,
                              gboolean    last,
                              TagReader  *tag_reader)
{
        GstPad *sink_pad;

        /**
         * The last discovered pad will always be the one hooked up to
         * the sink.
         **/
        sink_pad = gst_element_get_pad (tag_reader->priv->sink, "sink");
        gst_pad_link (pad, sink_pad);
}

/**
 * Data of an unknown type fed.
 **/
static void
decodebin_unknown_type_cb (GstElement *decodebin,
                           GstPad     *pad,
                           GstCaps    *caps,
                           TagReader  *tag_reader)
{
        tag_reader->priv->current_error =
                g_error_new (TAG_READER_ERROR,
                             TAG_READER_ERROR_UNKNOWN_TYPE,
                             "Unknown type");

        flush_head (tag_reader);
        feed_head (tag_reader);
}

/**
 * Fakesink hands over a buffer.
 **/
static void
fakesink_handoff_cb (GstElement *fakesink,
                     GstBuffer  *buffer,
                     GstPad     *pad,
                     TagReader  *tag_reader)
{
        GstStructure *structure;
        GstMessage *message;
        GstBus *bus;

        /**
         * Post a message to the bus, as we are in another thread here.
         **/
        structure = gst_structure_new ("handoff", NULL);
        message = gst_message_new_application (GST_OBJECT (fakesink),
                                               structure);

        bus = gst_pipeline_get_bus (GST_PIPELINE (tag_reader->priv->pipeline));
        gst_bus_post (bus, message);
        gst_object_unref (GST_OBJECT (bus));
}

/**
 * Constructs the GStreamer pipeline.
 **/
static void
construct_pipeline (TagReader *tag_reader)
{

        GstBus *bus;

        /**
         * The pipeline.
         **/
        tag_reader->priv->pipeline = gst_pipeline_new ("pipeline");

        /**
         * No src element yet.
         **/
        tag_reader->priv->src = NULL;

        /**
         * A decodebin.
         **/
        tag_reader->priv->decodebin = 
                gst_element_factory_make ("decodebin", "decodebin");
        if (!tag_reader->priv->decodebin) {
                g_warning ("No decodebin found. Tag reading will not work.");

                return;
        }

        gst_bin_add (GST_BIN (tag_reader->priv->pipeline),
                     tag_reader->priv->decodebin);

        g_signal_connect_object (tag_reader->priv->decodebin,
                                 "new-decoded-pad",
                                 G_CALLBACK (decodebin_new_decoded_pad_cb),
                                 tag_reader,
                                 0);
        g_signal_connect_object (tag_reader->priv->decodebin,
                                 "unknown-type",
                                 G_CALLBACK (decodebin_unknown_type_cb),
                                 tag_reader,
                                 0);

        /**
         * A fakesink.
         **/
        tag_reader->priv->sink =
                gst_element_factory_make ("fakesink", "fakesink");
        if (!tag_reader->priv->sink) {
                g_warning ("No fakesink found. Tag reading will not work.");

                return;
        }
        
        gst_bin_add (GST_BIN (tag_reader->priv->pipeline),
                     tag_reader->priv->sink);

        g_object_set (tag_reader->priv->sink,
                      "signal-handoffs", TRUE,
                      NULL);

        g_signal_connect_object (tag_reader->priv->sink,
                                 "handoff",
                                 G_CALLBACK (fakesink_handoff_cb),
                                 tag_reader,
                                 0);

        /**
         * Connect to signals on bus.
         **/
        bus = gst_pipeline_get_bus (GST_PIPELINE (tag_reader->priv->pipeline));

        gst_bus_add_signal_watch (bus);

        g_signal_connect_object (bus,
                                 "message::error",
                                 G_CALLBACK (bus_message_error_cb),
                                 tag_reader,
                                 0);
        g_signal_connect_object (bus,
                                 "message::eos",
                                 G_CALLBACK (bus_message_eos_cb),
                                 tag_reader,
                                 0);
        g_signal_connect_object (bus,
                                 "message::tag",
                                 G_CALLBACK (bus_message_tag_cb),
                                 tag_reader,
                                 0);
        g_signal_connect_object (bus,
                                 "message::application",
                                 G_CALLBACK (bus_message_application_cb),
                                 tag_reader,
                                 0);

        gst_object_unref (GST_OBJECT (bus));
}

static void
tag_reader_init (TagReader *tag_reader)
{
        /**
         * Create pointer to private data.
         **/
        tag_reader->priv =
                G_TYPE_INSTANCE_GET_PRIVATE (tag_reader,
                                             TYPE_TAG_READER,
                                             TagReaderPrivate);

        /**
         * Create scanning queue.
         **/
        tag_reader->priv->queue = g_queue_new ();

        /**
         * Initialize next ScanUriData ID.
         **/
        tag_reader->priv->next_id = 1;

        /**
         * No current URI yet.
         **/
        tag_reader->priv->current_error = NULL;
        tag_reader->priv->current_tag_list = NULL;

        /**
         * Construct GStreamer pipeline.
         **/
        construct_pipeline (tag_reader);
}

static void
tag_reader_dispose (GObject *object)
{
        TagReader *tag_reader;
        GObjectClass *object_class;

        tag_reader = TAG_READER (object);

        if (tag_reader->priv->pipeline) {
                gst_element_set_state (tag_reader->priv->pipeline,
                                       GST_STATE_NULL);

                gst_object_unref (GST_OBJECT (tag_reader->priv->pipeline));
                tag_reader->priv->pipeline = NULL;
        }

        object_class = G_OBJECT_CLASS (tag_reader_parent_class);
        object_class->dispose (object);
}

static void
tag_reader_finalize (GObject *object)
{
        TagReader *tag_reader;
        GObjectClass *object_class;

        tag_reader = TAG_READER (object);

        if (tag_reader->priv->current_error)
                g_error_free (tag_reader->priv->current_error);
        if (tag_reader->priv->current_tag_list)
                gst_tag_list_free (tag_reader->priv->current_tag_list);

        g_queue_foreach (tag_reader->priv->queue,
                         (GFunc) scan_uri_data_free,
                         NULL);
        g_queue_free (tag_reader->priv->queue);

        object_class = G_OBJECT_CLASS (tag_reader_parent_class);
        object_class->finalize (object);
}

static void
tag_reader_class_init (TagReaderClass *klass)
{
        GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->dispose  = tag_reader_dispose;
	object_class->finalize = tag_reader_finalize;

        g_type_class_add_private (klass, sizeof (TagReaderPrivate));

        signals[SIGNAL_URI_SCANNED] =
                g_signal_new ("uri-scanned",
                              TYPE_TAG_READER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (TagReaderClass, uri_scanned),
                              NULL,
                              NULL,
                              marshal_VOID__STRING_POINTER_POINTER,
                              G_TYPE_NONE,
                              3,
                              G_TYPE_STRING,
                              G_TYPE_POINTER,
                              G_TYPE_POINTER);
}

/**
 * tag_reader_new
 *
 * Return value: A new #TagReader.
 **/
TagReader *
tag_reader_new (void)
{
        return g_object_new (TYPE_TAG_READER, NULL);
}

/**
 * tag_reader_scan_uri
 * @tag_reader: A #TagReader
 * @uri: An URI
 *
 * Queues @uri up for tag reading.
 *
 * Return value: A scan ID as @guint.
 **/
guint
tag_reader_scan_uri (TagReader  *tag_reader,
                     const char *uri)
{
        ScanUriData *data;

        g_return_val_if_fail (IS_TAG_READER (tag_reader), 0);
        g_return_val_if_fail (uri != NULL, 0);

        data = g_slice_new (ScanUriData);

        data->uri = g_strdup (uri);
        data->id  = tag_reader->priv->next_id++;

        g_queue_push_tail (tag_reader->priv->queue, data);

        if (g_queue_get_length (tag_reader->priv->queue) == 1) {
                /**
                 * The queue was empty, so we were idle. This means
                 * we need to start the pump again by feeding the new
                 * uri to the pipeline.
                 **/
                feed_head (tag_reader);
        }

        return data->id;
}

/**
 * Find a ScanUriData by its ID.
 **/
static int
find_scan_uri_data (gconstpointer a,
                    gconstpointer b)
{
        guint ai = GPOINTER_TO_UINT (a);
        guint bi = GPOINTER_TO_UINT (b);

        if (ai < bi)
                return -1;
        else if (ai > bi)
                return 1;
        else
                return 0;
}

/**
 * tag_reader_cancal_scan_uri
 * @tag_reader: A #TagReader
 * @scan_id: The #guint scan ID as returned by #tag_reader_scan_uri
 *
 * Cancels the scanning of URI with ID @scan_id.
 **/
void
tag_reader_cancel_scan_uri (TagReader *tag_reader,
                            guint      scan_id)
{
        GList *link;

        g_return_if_fail (IS_TAG_READER (tag_reader));

        link = g_queue_find_custom (tag_reader->priv->queue,
                                    GUINT_TO_POINTER (scan_id),
                                    find_scan_uri_data);
        if (!link) {
                g_warning ("Not scanning URI with ID %u", scan_id);

                return;
        }

        if (!link->prev) {
                /**
                 * We were just processing this one. Use standard
                 * flushing process.
                 **/
                flush_head (tag_reader);
        } else {
                /**
                 * This one is queued up. Dequeue.
                 **/
                scan_uri_data_free (link->data);
                g_queue_delete_link (tag_reader->priv->queue, link);
        }
}

/**
 * Returns the tag reader error quark.
 **/
GQuark
tag_reader_error_quark (void)
{
        return g_quark_from_static_string ("tag-reader-error");
}
