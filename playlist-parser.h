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

#ifndef __PLAYLIST_PARSER_H__
#define __PLAYLIST_PARSER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
        PLAYLIST_PARSER_ERROR_UNKNOWN_TYPE,
        PLAYLIST_PARSER_ERROR_UNSUPPORTED_SCHEME
} PlaylistParserError;

GQuark playlist_parser_error_quark (void);

#define PLAYLIST_PARSER_ERROR \
                (playlist_parser_error_quark ())

#define TYPE_PLAYLIST_PARSER \
                (playlist_parser_get_type ())
#define PLAYLIST_PARSER(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 TYPE_PLAYLIST_PARSER, \
                 PlaylistParser))
#define PLAYLIST_PARSER_CLASS(klass) \
                (G_TYPE_CHECK_CLASS_CAST ((klass), \
                 TYPE_PLAYLIST_PARSER, \
                 PlaylistParserClass))
#define IS_PLAYLIST_PARSER(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 TYPE_PLAYLIST_PARSER))
#define IS_PLAYLIST_PARSER_CLASS(klass) \
                (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                 TYPE_PLAYLIST_PARSER))
#define PLAYLIST_PARSER_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 TYPE_PLAYLIST_PARSER, \
                 PlaylistParserClass))

typedef struct {
        GObject parent;

        gpointer _reserved;
} PlaylistParser;

typedef struct {
        GObjectClass parent_class;

        /* Signals */
        void (* playlist_start) (PlaylistParser *parser);
        void (* playlist_end)   (PlaylistParser *parser);
        void (* entry)          (PlaylistParser *parser,
                                 const char     *uri);

        /* Future padding */
        void (* _reserved1) (void);
        void (* _reserved2) (void);
        void (* _reserved3) (void);
        void (* _reserved4) (void);
} PlaylistParserClass;

GType
playlist_parser_get_type (void) G_GNUC_CONST;

PlaylistParser *
playlist_parser_new      (void);

gboolean
playlist_parser_parse    (PlaylistParser *parser,
                          const char     *uri,
                          GError        **error);

G_END_DECLS

#endif /* __PLAYLIST_PARSER_H__ */
