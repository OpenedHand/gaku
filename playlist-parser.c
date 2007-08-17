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

#include "playlist-parser.h"

G_DEFINE_TYPE (PlaylistParser,
               playlist_parser,
               G_TYPE_OBJECT);

enum {
        SIGNAL_PLAYLIST_START,
        SIGNAL_PLAYLIST_END,
        SIGNAL_ENTRY,
        SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static void
playlist_parser_init (PlaylistParser *parser)
{
}

static void
playlist_parser_dispose (GObject *object)
{
        PlaylistParser *parser;
        GObjectClass *object_class;

        parser = PLAYLIST_PARSER (object);

        object_class = G_OBJECT_CLASS (playlist_parser_parent_class);
        object_class->dispose (object);
}

static void
playlist_parser_finalize (GObject *object)
{
        PlaylistParser *parser;
        GObjectClass *object_class;

        parser = PLAYLIST_PARSER (object);

        object_class = G_OBJECT_CLASS (playlist_parser_parent_class);
        object_class->finalize (object);
}

static void
playlist_parser_class_init (PlaylistParserClass *klass)
{
        GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->dispose  = playlist_parser_dispose;
	object_class->finalize = playlist_parser_finalize;

        signals[SIGNAL_PLAYLIST_START] =
                g_signal_new ("playlist-start",
                              TYPE_PLAYLIST_PARSER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PlaylistParserClass,
                                               playlist_start),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        
        signals[SIGNAL_PLAYLIST_END] =
                g_signal_new ("playlist-end",
                              TYPE_PLAYLIST_PARSER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PlaylistParserClass,
                                               playlist_end),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals[SIGNAL_ENTRY] =
                g_signal_new ("entry",
                              TYPE_PLAYLIST_PARSER,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PlaylistParserClass,
                                               entry),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
}

/**
 * playlist_parser_new
 *
 * Return value: A new #PlaylistParser.
 **/
PlaylistParser *
playlist_parser_new (void)
{
        return g_object_new (TYPE_PLAYLIST_PARSER, NULL);
}

/**
 * Emit the 'entry' signal for @path after converting it to an URI.
 **/
static void
got_absolute_path (PlaylistParser *parser,
                   const char     *path)
{
        char *uri;

        uri = g_filename_to_uri (path, NULL, NULL);
        if (!uri)
                return;
        
        g_signal_emit (parser, signals[SIGNAL_ENTRY], 0, uri);

        g_free (uri);
}

/**
 * Parse @channel contents as M3U.
 **/
static void
parse_m3u (PlaylistParser *parser,
           GIOChannel     *channel,
           const char     *dirname)
{
        char *line, *p;
        gsize length;

        /**
         * Signal start of playlist.
         **/
        g_signal_emit (parser, signals[SIGNAL_PLAYLIST_START], 0);

        /**
         * Parse @channel line by line.
         **/
        while (g_io_channel_read_line (channel,
                                       &line,
                                       &length, 
                                       NULL,
                                       NULL) == G_IO_STATUS_NORMAL) {
                if (line[0] == '#') {
                        /**
                         * Ignore comments.
                         **/
                        g_free (line);

                        continue;
                }

                /**
                 * This is a normal line. First we de-DOS...
                 **/
                for (p = line; *p != '\0'; p++) {
                        switch (*p) {
                        case '\\':
                                *p = '/';
                                break;
                        case '\r':
                                *p = '\0';
                                break;
                        case '\n':
                                *p = '\0';
                                break;
                        default:
                                break;
                        }
                }

                /**
                 * Now we process it.
                 **/
                if (strstr (line, "://")) {
                        /**
                         * This already is an URI.
                         **/
                        g_signal_emit (parser, signals[SIGNAL_ENTRY], 0, line);
                } else if (g_path_is_absolute (line)) {
                        /**
                         * This is an absolute path.
                         **/
                        got_absolute_path (parser, line);
                } else {
                        char *absolute;

                        /**
                         * This is a relative path.
                         **/
                        absolute = g_build_filename (dirname, line, NULL);
                        got_absolute_path (parser, absolute);
                        g_free (absolute);
                }

                g_free (line);
        }
        
        /**
         * Signal end of playlist.
         **/
        g_signal_emit (parser, signals[SIGNAL_PLAYLIST_END], 0);
}

/**
 * playlist_parser_scan_uri
 * @parser: A #PlaylistParser
 * @uri: An URI
 * @error: Location where to store a #GError if an error occurs.
 *
 * Parse @uri.
 *
 * Return value: TRUE on success, FALSE if an error occured in which case
 * @error is set as well.
 **/
gboolean
playlist_parser_parse (PlaylistParser *parser,
                       const char     *uri,
                       GError        **error)
{
        char *ext, *filename, *dirname;
        GIOChannel *channel;
        
        g_return_val_if_fail (IS_PLAYLIST_PARSER (parser), FALSE);
        g_return_val_if_fail (uri != NULL, FALSE);

        /**
         * Does @uri point to a M3U file?
         **/
        ext = strrchr (uri, '.');
        if (!ext || g_ascii_strcasecmp (ext, ".m3u")) {
                g_set_error (error,
                             PLAYLIST_PARSER_ERROR,
                             PLAYLIST_PARSER_ERROR_UNKNOWN_TYPE,
                             "Unknown type");

                return FALSE;
        }

        /**
         * Does @uri point to a local file?
         **/
        if (g_ascii_strncasecmp (uri, "file:", 5)) {
                g_set_error (error,
                             PLAYLIST_PARSER_ERROR,
                             PLAYLIST_PARSER_ERROR_UNSUPPORTED_SCHEME,
                             "Unsupported scheme in URI '%s'",
                             uri);

                return FALSE;
        }

        /**
         * Convert @uri to a filename.
         **/
        filename = g_filename_from_uri (uri, NULL, error);
        if (!filename)
                return FALSE;

        /**
         * Open @filename for reading.
         **/
        channel = g_io_channel_new_file (filename, "r", error);
        if (!channel) {
                g_free (filename);

                return FALSE;
        }

        /**
         * Pass channel to parser.
         **/
        dirname = g_path_get_dirname (filename);
        parse_m3u (parser, channel, dirname);
        g_free (dirname);

        /**
         * Cleanup.
         **/
        g_io_channel_unref (channel);

        g_free (filename);

        return TRUE;
}

/**
 * Returns the playlist parser error quark.
 **/
GQuark
playlist_parser_error_quark (void)
{
        return g_quark_from_static_string ("playlist-parser-error");
}
