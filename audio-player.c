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

#include "audio-player.h"

G_DEFINE_TYPE (AudioPlayer,
               audio_player,
               G_TYPE_OBJECT);

struct _AudioPlayerPrivate {
        GstElement *playbin;

        char *uri;

        gboolean can_seek;

        int buffer_percent;

        int duration;

        guint tick_timeout_id;
};

enum {
        PROP_0,
        PROP_URI,
        PROP_PLAYING,
        PROP_POSITION,
        PROP_VOLUME,
        PROP_CAN_SEEK,
        PROP_BUFFER_PERCENT,
        PROP_DURATION
};

enum {
        TAG_LIST_AVAILABLE,
        EOS,
        ERROR,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

#define TICK_TIMEOUT 0.5

/* TODO: Possibly retrieve these through introspection. The problem is that we
 * need them in class_init already. */
#define GST_VOL_DEFAULT 1.0
#define GST_VOL_MAX     4.0

/**
 * An error occured.
 **/
static void
bus_message_error_cb (GstBus      *bus,
                      GstMessage  *message,
                      AudioPlayer *audio_player)
{
        GError *error;

        error = NULL;
        gst_message_parse_error (message,
                                 &error,
                                 NULL);
        
        g_signal_emit (audio_player,
                       signals[ERROR],
                       0,
                       error);

        g_error_free (error);

}

/**
 * End of stream reached.
 **/
static void
bus_message_eos_cb (GstBus      *bus,
                    GstMessage  *message,
                    AudioPlayer *audio_player)
{
        /**
         * Make sure UI is in sync.
         **/
        g_object_notify (G_OBJECT (audio_player), "position");

        /**
         * Emit EOS signal.
         **/
        g_signal_emit (audio_player,
                       signals[EOS],
                       0);
}

/**
 * Tag list available.
 **/
static void
bus_message_tag_cb (GstBus      *bus,
                    GstMessage  *message,
                    AudioPlayer *audio_player)
{
        GstTagList *tag_list;

        gst_message_parse_tag (message, &tag_list);

        g_signal_emit (audio_player,
                       signals[TAG_LIST_AVAILABLE],
                       0,
                       tag_list);

        gst_tag_list_free (tag_list);
}

/**
 * Buffering information available.
 **/
static void
bus_message_buffering_cb (GstBus      *bus,
                          GstMessage  *message,
                          AudioPlayer *audio_player)
{
        const GstStructure *str;

        str = gst_message_get_structure (message);
        if (!str)
                return;

        if (!gst_structure_get_int (str,
                                    "buffer-percent",
                                    &audio_player->priv->buffer_percent))
                return;
        
        g_object_notify (G_OBJECT (audio_player), "buffer-percent");
}

/**
 * Duration information available.
 **/
static void
bus_message_duration_cb (GstBus      *bus,
                         GstMessage  *message,
                         AudioPlayer *audio_player)
{
        GstFormat format;
        gint64 duration;

        gst_message_parse_duration (message,
                                    &format,
                                    &duration);

        if (format != GST_FORMAT_TIME)
                return;

        audio_player->priv->duration = duration / GST_SECOND;

        g_object_notify (G_OBJECT (audio_player), "duration");
}

/**
 * A state change occured.
 **/
static void
bus_message_state_change_cb (GstBus      *bus,
                             GstMessage  *message,
                             AudioPlayer *audio_player)
{
        gpointer src;
        GstState old_state, new_state;

        src = GST_MESSAGE_SRC (message);
        
        if (src != audio_player->priv->playbin)
                return;

        gst_message_parse_state_changed (message,
                                         &old_state,
                                         &new_state,
                                         NULL);

        if (old_state == GST_STATE_READY &&
            new_state == GST_STATE_PAUSED) {
                GstQuery *query;

                /**
                 * Determine whether we can seek.
                 **/
                query = gst_query_new_seeking (GST_FORMAT_TIME);

                if (gst_element_query (audio_player->priv->playbin, query)) {
                        gst_query_parse_seeking (query,
                                                 NULL,
                                                 &audio_player->priv->can_seek,
                                                 NULL,
                                                 NULL);
                } else {
                        /**
                         * Could not query for ability to seek. Determine
                         * using URI.
                         **/

                        if (g_str_has_prefix (audio_player->priv->uri,
                                              "http://")) {
                                audio_player->priv->can_seek = FALSE;
                        } else {
                                audio_player->priv->can_seek = TRUE;
                        }
                }

                gst_query_unref (query);
                
                g_object_notify (G_OBJECT (audio_player), "can-seek");

                /**
                 * Determine the duration.
                 **/
                query = gst_query_new_duration (GST_FORMAT_TIME);

                if (gst_element_query (audio_player->priv->playbin, query)) {
                        gint64 duration;

                        gst_query_parse_duration (query,
                                                  NULL,
                                                  &duration);

                        audio_player->priv->duration = duration / GST_SECOND;
                        
                        g_object_notify (G_OBJECT (audio_player), "duration");
                }

                gst_query_unref (query);
        }
}

/**
 * Called every TICK_TIMEOUT secs to notify of a position change.
 **/
static gboolean
tick_timeout (AudioPlayer *audio_player)
{
        g_object_notify (G_OBJECT (audio_player), "position");

        return TRUE;
}

/**
 * Constructs the GStreamer pipeline.
 **/
static void
construct_pipeline (AudioPlayer *audio_player)
{

        GstElement *audiosink;
        GstBus *bus;

        /**
         * playbin.
         **/
        audio_player->priv->playbin =
                gst_element_factory_make ("playbin", "playbin");
        if (!audio_player->priv->playbin) {
                g_warning ("No playbin found. Playback will not work.");

                return;
        }

        /**
         * An audiosink.
         **/
        audiosink = gst_element_factory_make ("gconfaudiosink", "audiosink");
        if (!audiosink) {
                g_warning ("No gconfaudiosink found. Trying autoaudiosink ...");

                audiosink = gst_element_factory_make ("autoaudiosink",
                                                      "audiosink");
                if (!audiosink) {
                        g_warning ("No autoaudiosink found. "
                                   "Trying alsasink ...");

                        audiosink = gst_element_factory_make ("alsasink",
                                                              "audiosink");
                        if (!audiosink) {
                                g_warning ("No audiosink could be found. "
                                           "Audio will not be available.");
                        }
                }
        }

        /**
         * Click sinks into playbin.
         **/
        g_object_set (G_OBJECT (audio_player->priv->playbin),
                      "audio-sink", audiosink,
                      "video-sink", NULL,
                      NULL);

        /**
         * Connect to signals on bus.
         **/
        bus = gst_pipeline_get_bus (GST_PIPELINE (audio_player->priv->playbin));

        gst_bus_add_signal_watch (bus);

        g_signal_connect_object (bus,
                                 "message::error",
                                 G_CALLBACK (bus_message_error_cb),
                                 audio_player,
                                 0);
        g_signal_connect_object (bus,
                                 "message::eos",
                                 G_CALLBACK (bus_message_eos_cb),
                                 audio_player,
                                 0);
        g_signal_connect_object (bus,
                                 "message::tag",
                                 G_CALLBACK (bus_message_tag_cb),
                                 audio_player,
                                 0);
        g_signal_connect_object (bus,
                                 "message::buffering",
                                 G_CALLBACK (bus_message_buffering_cb),
                                 audio_player,
                                 0);
        g_signal_connect_object (bus,
                                 "message::duration",
                                 G_CALLBACK (bus_message_duration_cb),
                                 audio_player,
                                 0);
        g_signal_connect_object (bus,
                                 "message::state-changed",
                                 G_CALLBACK (bus_message_state_change_cb),
                                 audio_player,
                                 0);

        gst_object_unref (GST_OBJECT (bus));
}

static void
audio_player_init (AudioPlayer *audio_player)
{
        /**
         * Create pointer to private data.
         **/
        audio_player->priv =
                G_TYPE_INSTANCE_GET_PRIVATE (audio_player,
                                             TYPE_AUDIO_PLAYER,
                                             AudioPlayerPrivate);

        /**
         * Construct GStreamer pipeline: playbin with sinks from GConf.
         **/
        construct_pipeline (audio_player);
}

static void
audio_player_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
        AudioPlayer *audio_player;

        audio_player = AUDIO_PLAYER (object);

        switch (property_id) {
        case PROP_URI:
                audio_player_set_uri (audio_player,
                                      g_value_get_string (value));
                break;
        case PROP_PLAYING:
                audio_player_set_playing (audio_player,
                                          g_value_get_boolean (value));
                break;
        case PROP_POSITION:
                audio_player_set_position (audio_player,
                                           g_value_get_int (value));
                break;
        case PROP_VOLUME:
                audio_player_set_volume (audio_player,
                                         g_value_get_double (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
audio_player_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        AudioPlayer *audio_player;

        audio_player = AUDIO_PLAYER (object);

        switch (property_id) {
        case PROP_URI:
                g_value_set_string
                        (value,
                         audio_player_get_uri (audio_player));
                break;
        case PROP_PLAYING:
                g_value_set_boolean
                        (value,
                         audio_player_get_playing (audio_player));
                break;
        case PROP_POSITION:
                g_value_set_int
                        (value,
                         audio_player_get_position (audio_player));
                break;
        case PROP_VOLUME:
                g_value_set_double
                        (value,
                         audio_player_get_volume (audio_player));
                break;
        case PROP_CAN_SEEK:
                g_value_set_boolean
                        (value,
                         audio_player_get_can_seek (audio_player));
                break;
        case PROP_BUFFER_PERCENT:
                g_value_set_int
                        (value,
                         audio_player_get_buffer_percent (audio_player));
                break;
        case PROP_DURATION:
                g_value_set_int
                        (value,
                         audio_player_get_duration (audio_player));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
audio_player_dispose (GObject *object)
{
        AudioPlayer *audio_player;
        GObjectClass *object_class;

        audio_player = AUDIO_PLAYER (object);

        if (audio_player->priv->playbin) {
                gst_element_set_state (audio_player->priv->playbin,
                                       GST_STATE_NULL);

                gst_object_unref (GST_OBJECT (audio_player->priv->playbin));
                audio_player->priv->playbin = NULL;
        }

        if (audio_player->priv->tick_timeout_id > 0) {
                g_source_remove (audio_player->priv->tick_timeout_id);
                audio_player->priv->tick_timeout_id = 0;
        }

        object_class = G_OBJECT_CLASS (audio_player_parent_class);
        object_class->dispose (object);
}

static void
audio_player_finalize (GObject *object)
{
        AudioPlayer *audio_player;
        GObjectClass *object_class;

        audio_player = AUDIO_PLAYER (object);

        g_free (audio_player->priv->uri);

        object_class = G_OBJECT_CLASS (audio_player_parent_class);
        object_class->finalize (object);
}

static void
audio_player_class_init (AudioPlayerClass *klass)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = audio_player_set_property;
        object_class->get_property = audio_player_get_property;
        object_class->dispose      = audio_player_dispose;
        object_class->finalize     = audio_player_finalize;

        g_type_class_add_private (klass, sizeof (AudioPlayerPrivate));

        g_object_class_install_property
                (object_class,
                 PROP_URI,
                 g_param_spec_string
                         ("uri",
                          "URI",
                          "The loaded URI.",
                          NULL,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        g_object_class_install_property
                (object_class,
                 PROP_PLAYING,
                 g_param_spec_boolean
                         ("playing",
                          "Playing",
                          "TRUE if playing.",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        g_object_class_install_property
                (object_class,
                 PROP_POSITION,
                 g_param_spec_int
                         ("position",
                          "Position",
                          "The position in the current stream in seconds.",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        g_object_class_install_property
                (object_class,
                 PROP_VOLUME,
                 g_param_spec_double
                         ("volume",
                          "Volume",
                          "The audio volume.",
                          0, GST_VOL_MAX, GST_VOL_DEFAULT,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        g_object_class_install_property
                (object_class,
                 PROP_CAN_SEEK,
                 g_param_spec_boolean
                         ("can-seek",
                          "Can seek",
                          "TRUE if the current stream is seekable.",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        g_object_class_install_property
                (object_class,
                 PROP_BUFFER_PERCENT,
                 g_param_spec_int
                         ("buffer-percent",
                          "Buffer percent",
                          "The percentage the current stream buffer is filled.",
                          0, 100, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        g_object_class_install_property
                (object_class,
                 PROP_DURATION,
                 g_param_spec_int
                         ("duration",
                          "Duration",
                          "The duration of the current stream in seconds.",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

        signals[TAG_LIST_AVAILABLE] =
                g_signal_new ("tag-list-available",
                              TYPE_AUDIO_PLAYER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (AudioPlayerClass,
                                               tag_list_available),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);

        signals[EOS] =
                g_signal_new ("eos",
                              TYPE_AUDIO_PLAYER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (AudioPlayerClass,
                                               eos),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals[ERROR] =
                g_signal_new ("error",
                              TYPE_AUDIO_PLAYER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (AudioPlayerClass,
                                               error),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);
}

/**
 * audio_player_new
 *
 * Return value: A new #AudioPlayer.
 **/
AudioPlayer *
audio_player_new (void)
{
        return g_object_new (TYPE_AUDIO_PLAYER, NULL);
}

/**
 * audio_player_set_uri
 * @audio_player: A #AudioPlayer
 * @uri: A URI
 *
 * Loads @uri.
 **/
void
audio_player_set_uri (AudioPlayer *audio_player,
                      const char  *uri)
{
        GstState state, pending;

        g_return_if_fail (IS_AUDIO_PLAYER (audio_player));

        if (!audio_player->priv->playbin)
                return;

        g_free (audio_player->priv->uri);

        if (uri) {
                audio_player->priv->uri = g_strdup (uri);

                /**
                 * Ensure the tick timeout is installed.
                 * 
                 * We also have it installed in PAUSED state, because
                 * seeks etc may have a delayed effect on the position.
                 **/
                if (audio_player->priv->tick_timeout_id == 0) {
                        audio_player->priv->tick_timeout_id =
                                g_timeout_add (TICK_TIMEOUT * 1000,
                                               (GSourceFunc) tick_timeout,
                                               audio_player);
                }
        } else {
                audio_player->priv->uri = NULL;

                /**
                 * Remove tick timeout.
                 **/
                if (audio_player->priv->tick_timeout_id > 0) {
                        g_source_remove (audio_player->priv->tick_timeout_id);
                        audio_player->priv->tick_timeout_id = 0;
                }
        }

        /**
         * Reset properties.
         **/
        audio_player->priv->can_seek = FALSE;
        audio_player->priv->duration = 0;

        /**
         * Store old state.
         **/
        gst_element_get_state (audio_player->priv->playbin,
                               &state,
                               &pending,
                               0);
        if (pending)
                state = pending;

        /**
         * State to NULL.
         **/
        gst_element_set_state (audio_player->priv->playbin, GST_STATE_NULL);

        /**
         * Set new URI.
         **/
        g_object_set (audio_player->priv->playbin,
                      "uri", uri,
                      NULL);
        
        /**
         * Restore state.
         **/
        if (uri)
                gst_element_set_state (audio_player->priv->playbin, state);

        /**
         * Emit notififications for all these to make sure UI is not showing
         * any properties of the old URI.
         **/
        g_object_notify (G_OBJECT (audio_player), "uri");
        g_object_notify (G_OBJECT (audio_player), "can-seek");
        g_object_notify (G_OBJECT (audio_player), "duration");
        g_object_notify (G_OBJECT (audio_player), "position");
}

/**
 * audio_player_get_uri
 * @audio_player: A #AudioPlayer
 *
 * Return value: The loaded URI, or NULL if none set.
 **/
const char *
audio_player_get_uri (AudioPlayer *audio_player)
{
        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), NULL);

        return audio_player->priv->uri;
}

/**
 * audio_player_set_playing
 * @audio_player: A #AudioPlayer
 * @playing: TRUE if @audio_player should be playing, FALSE otherwise
 *
 * Sets the playback state of @audio_player to @playing.
 **/
void
audio_player_set_playing (AudioPlayer *audio_player,
                          gboolean     playing)
{
        g_return_if_fail (IS_AUDIO_PLAYER (audio_player));

        if (!audio_player->priv->playbin)
                return;
        
        /**
         * Choose the correct state for the pipeline.
         **/
        if (audio_player->priv->uri) {
                GstState state;

                if (playing)
                        state = GST_STATE_PLAYING;
                else
                        state = GST_STATE_PAUSED;

                gst_element_set_state (audio_player->priv->playbin, state);
        } else {
                if (playing)
                        g_warning ("Tried to play, but no URI is loaded.");

                /**
                 * Do nothing.
                 **/
        }

        g_object_notify (G_OBJECT (audio_player), "playing");

        /**
         * Make sure UI is in sync.
         **/
        g_object_notify (G_OBJECT (audio_player), "position");
}

/**
 * audio_player_get_playing
 * @audio_player: A #AudioPlayer
 *
 * Return value: TRUE if @audio_player is playing.
 **/
gboolean
audio_player_get_playing (AudioPlayer *audio_player)
{
        GstState state, pending;

        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), FALSE);

        if (!audio_player->priv->playbin)
                return FALSE;

        gst_element_get_state (audio_player->priv->playbin,
                               &state,
                               &pending,
                               0);

        if (pending)
                return (pending == GST_STATE_PLAYING);
        else
                return (state == GST_STATE_PLAYING);
}

/**
 * audio_player_set_position
 * @audio_player: A #AudioPlayer
 * @position: The position in the current stream in seconds.
 *
 * Sets the position in the current stream to @position.
 **/
void
audio_player_set_position (AudioPlayer *audio_player,
                           int          position)
{
        GstState state, pending;

        g_return_if_fail (IS_AUDIO_PLAYER (audio_player));

        if (!audio_player->priv->playbin)
                return;

        /**
         * Store old state.
         **/
        gst_element_get_state (audio_player->priv->playbin,
                               &state,
                               &pending,
                               0);
        if (pending)
                state = pending;

        /**
         * State to PAUSED.
         **/
        gst_element_set_state (audio_player->priv->playbin, GST_STATE_PAUSED);
        
        /**
         * Perform the seek.
         **/
        gst_element_seek (audio_player->priv->playbin,
                          1.0,
                          GST_FORMAT_TIME,
                          GST_SEEK_FLAG_FLUSH,
                          GST_SEEK_TYPE_SET,
                          position * GST_SECOND,
                          0, 0);
        /**
         * Restore state.
         **/
        gst_element_set_state (audio_player->priv->playbin, state);
}

/**
 * audio_player_get_position
 * @audio_player: A #AudioPlayer
 *
 * Return value: The position in the current file in seconds.
 **/
int
audio_player_get_position (AudioPlayer *audio_player)
{
        GstQuery *query;
        gint64 position;
       
        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), -1);

        if (!audio_player->priv->playbin)
                return -1;

        query = gst_query_new_position (GST_FORMAT_TIME);

        if (gst_element_query (audio_player->priv->playbin, query)) {
                gst_query_parse_position (query,
                                          NULL,
                                          &position);
        } else
                position = 0;

        gst_query_unref (query);

        return (position / GST_SECOND);
}

/**
 * audio_player_set_volume
 * @audio_player: A #AudioPlayer
 * @volume: The audio volume to set, in the range 0.0 - 4.0.
 *
 * Sets the current audio volume to @volume.
 **/
void
audio_player_set_volume (AudioPlayer *audio_player,
                         double       volume)
{
        g_return_if_fail (IS_AUDIO_PLAYER (audio_player));
        g_return_if_fail (volume >= 0.0 && volume <= GST_VOL_MAX);

        if (!audio_player->priv->playbin)
                return;

        g_object_set (G_OBJECT (audio_player->priv->playbin),
                      "volume", volume,
                      NULL);
        
        g_object_notify (G_OBJECT (audio_player), "volume");
}

/**
 * audio_player_get_volume
 * @audio_player: A #AudioPlayer
 *
 * Return value: The current audio volume, in the range 0.0 - 4.0.
 **/
double
audio_player_get_volume (AudioPlayer *audio_player)
{
        double volume;

        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), 0);

        if (!audio_player->priv->playbin)
                return 0.0;

        g_object_get (audio_player->priv->playbin,
                      "volume", &volume,
                      NULL);

        return volume;
}

/**
 * audio_player_get_can_seek
 * @audio_player: A #AudioPlayer
 *
 * Return value: TRUE if the current stream is seekable.
 **/
gboolean
audio_player_get_can_seek (AudioPlayer *audio_player)
{
        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), FALSE);

        return audio_player->priv->can_seek;
}

/**
 * audio_player_get_buffer_percent
 * @audio_player: A #AudioPlayer
 *
 * Return value: Percentage the current stream buffer is filled.
 **/
int
audio_player_get_buffer_percent (AudioPlayer *audio_player)
{
        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), -1);

        return audio_player->priv->buffer_percent;
}

/**
 * audio_player_get_duration
 * @audio_player: A #AudioPlayer
 *
 * Return value: The duration of the current stream in seconds.
 **/
int
audio_player_get_duration (AudioPlayer *audio_player)
{
        g_return_val_if_fail (IS_AUDIO_PLAYER (audio_player), -1);

        return audio_player->priv->duration;
}
