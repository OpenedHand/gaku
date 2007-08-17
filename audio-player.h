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

#ifndef __AUDIO_PLAYER_H__
#define __AUDIO_PLAYER_H__

#include <glib-object.h>
#include <gst/gsttaglist.h>

G_BEGIN_DECLS

#define TYPE_AUDIO_PLAYER \
                (audio_player_get_type ())
#define AUDIO_PLAYER(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 TYPE_AUDIO_PLAYER, \
                 AudioPlayer))
#define AUDIO_PLAYER_CLASS(klass) \
                (G_TYPE_CHECK_CLASS_CAST ((klass), \
                 TYPE_AUDIO_PLAYER, \
                 AudioPlayerClass))
#define IS_AUDIO_PLAYER(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 TYPE_AUDIO_PLAYER))
#define IS_AUDIO_PLAYER_CLASS(klass) \
                (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                 TYPE_AUDIO_PLAYER))
#define AUDIO_PLAYER_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 TYPE_AUDIO_PLAYER, \
                 AudioPlayerClass))

typedef struct _AudioPlayerPrivate AudioPlayerPrivate;

typedef struct {
        GObject parent;

        AudioPlayerPrivate *priv;
} AudioPlayer;

typedef struct {
        GObjectClass parent_class;

        /* Signals */
        void (* tag_list_available) (AudioPlayer *audio_player,
                                     GstTagList  *tag_list);
        void (* eos)                (AudioPlayer *audio_player);
        void (* error)              (AudioPlayer *audio_player,
                                     GError      *error);
        
        /* Future padding */
        void (* _reserved1) (void);
        void (* _reserved2) (void);
        void (* _reserved3) (void);
        void (* _reserved4) (void);
} AudioPlayerClass;

GType
audio_player_get_type           (void) G_GNUC_CONST;

AudioPlayer *
audio_player_new                (void);

void
audio_player_set_uri            (AudioPlayer *audio_player,
                                 const char  *uri);

const char *
audio_player_get_uri            (AudioPlayer *audio_player);

void
audio_player_set_playing        (AudioPlayer *audio_player,
                                 gboolean     playing);

gboolean
audio_player_get_playing        (AudioPlayer *audio_player);

void
audio_player_set_position       (AudioPlayer *audio_player,
                                 int          position);

int
audio_player_get_position       (AudioPlayer *audio_player);

void
audio_player_set_volume         (AudioPlayer *audio_player,
                                 double       volume);

double
audio_player_get_volume         (AudioPlayer *audio_player);

gboolean
audio_player_get_can_seek       (AudioPlayer *audio_player);

int
audio_player_get_buffer_percent (AudioPlayer *audio_player);

int
audio_player_get_duration       (AudioPlayer *audio_player);

G_END_DECLS

#endif /* __AUDIO_PLAYER_H__ */
