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

#ifndef __TAG_READER_H__
#define __TAG_READER_H__

#include <glib-object.h>
#include <gst/gsttaglist.h>

G_BEGIN_DECLS

typedef enum {
        TAG_READER_ERROR_UNKNOWN_TYPE
} TagReaderError;

GQuark tag_reader_error_quark (void);

#define TAG_READER_ERROR \
                (tag_reader_error_quark ())

#define TYPE_TAG_READER \
                (tag_reader_get_type ())
#define TAG_READER(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 TYPE_TAG_READER, \
                 TagReader))
#define TAG_READER_CLASS(klass) \
                (G_TYPE_CHECK_CLASS_CAST ((klass), \
                 TYPE_TAG_READER, \
                 TagReaderClass))
#define IS_TAG_READER(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 TYPE_TAG_READER))
#define IS_TAG_READER_CLASS(klass) \
                (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                 TYPE_TAG_READER))
#define TAG_READER_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 TYPE_TAG_READER, \
                 TagReaderClass))

typedef struct _TagReaderPrivate TagReaderPrivate;

typedef struct {
        GObject parent;

        TagReaderPrivate *priv;
} TagReader;

typedef struct {
        GObjectClass parent_class;

        /* Signals */
        void (* uri_scanned) (TagReader  *tag_reader,
                              const char *uri,
                              GError     *error,
                              GstTagList *tag_list);

        /* Future padding */
        void (* _reserved1) (void);
        void (* _reserved2) (void);
        void (* _reserved3) (void);
        void (* _reserved4) (void);
} TagReaderClass;

GType
tag_reader_get_type        (void) G_GNUC_CONST;

TagReader *
tag_reader_new             (void);

guint
tag_reader_scan_uri        (TagReader  *tag_reader,
                            const char *uri);

void
tag_reader_cancel_scan_uri (TagReader  *tag_reader,
                            guint       scan_id);

G_END_DECLS

#endif /* __TAG_READER_H__ */
