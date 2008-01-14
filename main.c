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
#include <gtk/gtk.h>
#include <string.h>

#include "audio-player.h"
#include "playlist-parser.h"
#include "tag-reader.h"

typedef struct {
        /**
         * Our special objects.
         **/
        AudioPlayer    *audio_player;
        PlaylistParser *playlist_parser;
        TagReader      *tag_reader;

        /**
         * UI objects.
         **/
        GtkWidget *window;
        GtkWidget *play_pause_button;
        GtkWidget *previous_button;
        GtkWidget *next_button;
        GtkWidget *tree_view;

        GtkListStore *list_store;
        GtkTreeRowReference *playing_row;

        char *last_folder;
} AppData;

enum {
        COL_TITLE,
        COL_ARTIST,
        COL_URI
};

/**
 * Returns TRUE if @iter is the currently playing row.
 **/
static gboolean
iter_is_playing_row (AppData     *data,
                     GtkTreeIter *iter)
{
        GtkTreePath *path, *playing_path;
        gboolean retval;

        if (!data->playing_row)
                return FALSE;

        playing_path = gtk_tree_row_reference_get_path (data->playing_row);
        if (playing_path == NULL)
                /* The currently playing row has been deleted */
                return FALSE;

        path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->list_store),
                                        iter);

        retval = (gtk_tree_path_compare (path, playing_path) == 0);

        gtk_tree_path_free (playing_path);
        gtk_tree_path_free (path);

        return retval;
}

static void
update_title (AppData *data, const char *title)
{
        char *win_title;
        
        if (title) {
                win_title = g_strdup_printf ("%s - Music Player", title);
                gtk_window_set_title (GTK_WINDOW (data->window), win_title);
                g_free (win_title);
        } else {
                gtk_window_set_title (GTK_WINDOW (data->window), "Music Player");
        }
}

/**
 * Set @iter to be the playing row.
 **/
static void
set_playing_row (AppData     *data,
                 GtkTreeIter *iter)
{
        GtkTreeModel *tree_model;
        GtkTreePath *path;

        tree_model = GTK_TREE_MODEL (data->list_store);

        if (data->playing_row) {
                GtkTreeIter playing_iter;

                path = gtk_tree_row_reference_get_path (data->playing_row);

                /**
                 * Free old playing row.
                 **/
                gtk_tree_row_reference_free (data->playing_row);
                data->playing_row = NULL;

                /**
                 * Emit changed signal for old playing row.
                 **/
                gtk_tree_model_get_iter (tree_model, &playing_iter, path);
                gtk_tree_model_row_changed (tree_model, path, &playing_iter);
        }

        if (iter) {
                char *uri, *title;

                path = gtk_tree_model_get_path (tree_model, iter);

                /**
                 * Create new playing row.
                 **/
                data->playing_row = gtk_tree_row_reference_new (tree_model,
                                                                path);

                /**
                 * Emit changed signal for new playing row.
                 **/
                gtk_tree_model_row_changed (tree_model, path, iter);

                gtk_tree_path_free (path);
                
                /**
                 * Get data off new playing row.
                 **/
                gtk_tree_model_get (tree_model,
                                    iter,
                                    COL_URI, &uri,
                                    COL_TITLE, &title,
                                    -1);

                audio_player_set_uri (data->audio_player, uri);

                update_title (data, title);

                /* TODO show song metadata */

                g_free (uri);
                g_free (title);
        } else {
                data->playing_row = NULL;
                /**
                 * No playing row. Reset window title.
                 **/
                update_title (data, NULL);
                /* TODO hide metadata */
        }
}

/**
 * Skip to the previous song.
 **/
static gboolean
previous (AppData *data)
{
        GtkTreePath *path;
        GtkTreeIter iter;

        if (!data->playing_row)
                return FALSE;

        path = gtk_tree_row_reference_get_path (data->playing_row);
        if (!gtk_tree_path_prev (path)) {
                gtk_tree_path_free (path);

                return FALSE;
        }

        gtk_tree_model_get_iter (GTK_TREE_MODEL (data->list_store), 
                                 &iter, path);
        gtk_tree_path_free (path);
        
        set_playing_row (data, &iter);
        
        return TRUE;
}

/**
 * Skip to the next song.
 **/
static gboolean
next (AppData *data)
{
        GtkTreeModel *tree_model;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (!data->playing_row)
                return FALSE;

        tree_model = GTK_TREE_MODEL (data->list_store);

        path = gtk_tree_row_reference_get_path (data->playing_row);
        gtk_tree_model_get_iter (tree_model, &iter, path);
        gtk_tree_path_free (path);
        
        if (!gtk_tree_model_iter_next (tree_model, &iter))
                return FALSE;

        set_playing_row (data, &iter);
        
        return TRUE;
}

/**
 * End of stream reached.
 **/
static void
eos_cb (AudioPlayer *player,
        AppData     *data)
{
        /**
         * Go to next song.
         **/
        next (data);
}

/**
 * We are loading a new playlist.
 **/
static void
playlist_start_cb (PlaylistParser *parser,
                   AppData        *data)
{
        set_playing_row (data, NULL);

        /**
         * Clear playlist.
         **/
        gtk_list_store_clear (data->list_store);
}

/**
 * Add an URI to the playlist.
 **/
static void
add_uri (AppData    *data,
         const char *uri)
{
        GtkTreeIter iter;
        char *filename, *basename;

        filename = g_filename_from_uri (uri, NULL, NULL);
        if (!filename)
                return;

        /**
         * Display the file's basename by default.
         **/
        basename = g_path_get_basename (filename);
        g_free (filename);
        
        /**
         * Add to playlist.
         **/
        gtk_list_store_insert_with_values (data->list_store,
                                           &iter,
                                           -1,
                                           COL_TITLE, basename,
                                           COL_ARTIST, "",
                                           COL_URI, uri,
                                           -1);

        g_free (basename);

        /**
         * Feed to tag reader.
         **/
        tag_reader_scan_uri (data->tag_reader, uri);

        /**
         * Play this song if nothing is playing.
         **/
        if (!data->playing_row) {
                set_playing_row (data, &iter);
                
                gtk_toggle_button_set_active
                  (GTK_TOGGLE_BUTTON (data->play_pause_button), TRUE);
        }
}

/**
 * TagReader is done scanning an URI. Update UI.
 **/
static void
tag_reader_uri_scanned_cb (TagReader  *tag_reader,
                           const char *uri,
                           GError     *error,
                           GstTagList *tag_list,
                           AppData    *data)
{
        GtkTreeModel *tree_model;
        GtkTreeIter iter;
        char *title, *artist;
        
        if (error) {
                g_warning (error->message);

                return;
        }

        if (!tag_list)
                return;

        /**
         * Find appropriate row(s).
         *
         * XXX For bigger playlists this is gonna be slow. Having a
         * URI->iter(s) map would be faster if big playlists are a common
         * case.
         **/
        tree_model = GTK_TREE_MODEL (data->list_store);

        if (!gtk_tree_model_get_iter_first (tree_model, &iter))
                return;
        
        gst_tag_list_get_string (tag_list,
                                 GST_TAG_TITLE,
                                 &title);
        gst_tag_list_get_string (tag_list,
                                 GST_TAG_ARTIST,
                                 &artist);

        do {
                char *this_uri;

                gtk_tree_model_get (tree_model,
                                    &iter,
                                    COL_URI,
                                    &this_uri,
                                    -1);

                if (!strcmp (uri, this_uri)) {
                        /**
                         * Found a matching row.
                         **/
                        gtk_list_store_set (data->list_store,
                                            &iter,
                                            COL_TITLE, title,
                                            COL_ARTIST, artist,
                                            -1);

                        if (iter_is_playing_row (data, &iter)) {
                                /**
                                 * This is the playing row as well.
                                 * Update window title.
                                 **/
                                update_title (data, title);
                        }
                }

                g_free (this_uri);
        } while (gtk_tree_model_iter_next (tree_model, &iter));

        g_free (title);
        g_free (artist);
}

/**
 * Window deleted. Quit app.
 **/
static gboolean
window_delete_event_cb (GtkWidget *window,
                        GdkEvent  *event,
                        gpointer   user_data)
{
        gtk_main_quit ();
        
        return TRUE;
}

/**
 * 'Play/Pause' button clicked.
 **/
static void
play_pause_button_toggled_cb (GtkToggleButton *button,
                              AppData         *data)
{
        audio_player_set_playing (data->audio_player, button->active);
}

/**
 * 'Previous' button clicked.
 **/
static void
previous_button_clicked_cb (GtkButton *button,
                            AppData   *data)
{
        previous (data);
}

/**
 * 'Next' button clicked.
 **/
static void
next_button_clicked_cb (GtkButton *button,
                        AppData   *data)
{
        next (data);
}

#if 0
/**
 * 'Open playlist' button clicked.
 **/
static void
open_playlist_button_clicked_cb (GtkButton *button,
                                 AppData   *data)
{
        GtkWidget *dialog;

        dialog = gtk_file_chooser_dialog_new ("Open Playlist",
                                              GTK_WINDOW (data->window),
                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                              GTK_STOCK_CANCEL,
                                              GTK_RESPONSE_CANCEL,
                                              GTK_STOCK_OPEN,
                                              GTK_RESPONSE_ACCEPT,
                                              NULL);

        switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
        default:
                /* Fall through */
        case GTK_RESPONSE_CANCEL:
                break;
        case GTK_RESPONSE_ACCEPT:
        {
                char *uri;
                GError *error;

                uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));

                error = NULL;
                if (!playlist_parser_parse (data->playlist_parser,
                                            uri, &error)) {
                        g_warning (error->message);

                        g_error_free (error);
                }
                
                g_free (uri);
                
                break;
        }
        }

        gtk_widget_destroy (dialog);
}
#endif

/**
 * 'Add song' button clicked.
 **/
static void
add_song_button_clicked_cb (GtkButton *button,
                            AppData   *data)
{
        GtkWidget *dialog;
        GtkFileChooser *chooser;

        dialog = gtk_file_chooser_dialog_new ("Add Song",
                                              GTK_WINDOW (data->window),
                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                              GTK_STOCK_CANCEL,
                                              GTK_RESPONSE_CANCEL,
                                              GTK_STOCK_OPEN,
                                              GTK_RESPONSE_ACCEPT,
                                              NULL);
        chooser = GTK_FILE_CHOOSER (dialog);

        gtk_file_chooser_set_select_multiple (chooser, TRUE);
        
        if (data->last_folder) {
                gtk_file_chooser_set_current_folder_uri (chooser, data->last_folder);
        }

        switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
        default:
                /* Fall through */
        case GTK_RESPONSE_CANCEL:
                break;
        case GTK_RESPONSE_ACCEPT:
        {
                GSList *uris;

                if (data->last_folder) {
                        g_free (data->last_folder);
                }
                data->last_folder = gtk_file_chooser_get_current_folder_uri (chooser);
                
                uris = gtk_file_chooser_get_uris (chooser);
                
                while (uris) {
                        char *uri = uris->data;
                        
                        add_uri (data, uri);
                        g_free (uri);
                        
                        uris = g_slist_delete_link (uris, uris);
                }
                break;
        }
        }

        gtk_widget_destroy (dialog);
}

/**
 * 'Remove song' button clicked.
 **/
static void
remove_song_button_clicked_cb (GtkButton *button,
                               AppData   *data)
{
        GtkTreeModel *model = GTK_TREE_MODEL (data->list_store);
        GtkTreeSelection *selection;
        GList *rows, *l;
        GtkTreeIter iter;
        
        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->tree_view));

        rows = gtk_tree_selection_get_selected_rows (selection, NULL);
        
        if (rows == NULL)
          return;

        /* Convert the paths to row references */
        for (l = rows; l; l = l->next) {
                GtkTreePath *path = l->data;
                GtkTreeRowReference *ref;
                
                ref = gtk_tree_row_reference_new (model, path);
                gtk_tree_path_free (path);
                l->data = ref;
        }
        
        /* Remove the rows */
        for (l = rows; l; l = l->next) {
                GtkTreePath *path;
                
                path = gtk_tree_row_reference_get_path (l->data);

                gtk_tree_model_get_iter (model, &iter, path);

                /* If this song was playing, try and play the next song. */
                if (iter_is_playing_row (data, &iter)) {
                        GtkTreeIter iter2;
                        path = gtk_tree_path_copy (path);
                        gtk_tree_path_next (path);
                        if (gtk_tree_model_get_iter (model, &iter2, path)) {
                                set_playing_row (data, &iter2);
                        } else {
                                set_playing_row (data, NULL);
                                gtk_toggle_button_set_active
                                        (GTK_TOGGLE_BUTTON (data->play_pause_button), FALSE);
                        }
                        gtk_tree_path_free (path);
                }

                gtk_list_store_remove (data->list_store, &iter);
        }
        
        g_list_foreach (rows, (GFunc)gtk_tree_row_reference_free, NULL);
        g_list_free (rows);
}

/**
 * Tree view row activated.
 **/
static void
row_activated_cb (GtkTreeView       *tree_view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  AppData           *data)
{
        GtkTreeIter iter;
        
        gtk_tree_model_get_iter (GTK_TREE_MODEL (data->list_store),
                                 &iter,
                                 path);
        
        set_playing_row (data, &iter);
}

/**
 * 'Playing' column cell data function.
 **/
static void
playing_cell_func (GtkTreeViewColumn *col,
                   GtkCellRenderer   *cell,
                   GtkTreeModel      *model,
                   GtkTreeIter       *iter,
                   AppData           *data)
{
        const char *stock_id;
        
        if (iter_is_playing_row (data, iter))
                stock_id = GTK_STOCK_MEDIA_PLAY;
        else
                stock_id = NULL;

        g_object_set (cell,
                      "stock-size", GTK_ICON_SIZE_MENU,
                      "stock-id", stock_id,
                      NULL);
}

/**
 * Text column cell data function.
 **/
static void
text_cell_func (GtkTreeViewColumn *col,
                GtkCellRenderer   *cell,
                GtkTreeModel      *model,
                GtkTreeIter       *iter,
                gpointer           data)
{
        char *title, *artist, *text;

        gtk_tree_model_get (model, iter,
                            COL_TITLE, &title,
                            COL_ARTIST, &artist,
                            -1);

        text = g_markup_printf_escaped ("<b>%s</b>\n%s", title, artist);

        g_free (artist);
        g_free (title);

        g_object_set (cell, "markup", text, NULL);

        g_free (text);
}

/**
 * Main.
 **/
int
main (int argc, char **argv)
{
        AppData *data;
        GtkWidget *vbox, *hbox, *bbox, *scrolled_window;
        GtkWidget *button, *image;

        /**
         * Initialize APIs.
         **/
        gst_init (&argc, &argv);
        gtk_init (&argc, &argv);

        /**
         * Create AppData structure.
         **/
        data = g_slice_new0 (AppData);

        /**
         * Set up AudioPlayer.
         **/
        data->audio_player = audio_player_new ();

        g_signal_connect (data->audio_player,
                          "eos",
                          G_CALLBACK (eos_cb),
                          data);

        /**
         * Set up PlaylistParser.
         **/
        data->playlist_parser = playlist_parser_new ();

        g_signal_connect (data->playlist_parser,
                          "playlist-start",
                          G_CALLBACK (playlist_start_cb),
                          data);

        g_signal_connect_swapped (data->playlist_parser,
                                  "entry",
                                  G_CALLBACK (add_uri),
                                  data);
        
        /**
         * Set up TagReader.
         **/
        data->tag_reader = tag_reader_new ();

        g_signal_connect (data->tag_reader,
                          "uri-scanned",
                          G_CALLBACK (tag_reader_uri_scanned_cb),
                          data);

        /**
         * Create UI.
         **/
        gtk_window_set_default_icon_name ("audio-player");
        
        data->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

        gtk_window_set_default_size (GTK_WINDOW (data->window),
                                     400, 500);

        gtk_container_set_border_width (GTK_CONTAINER (data->window), 4);
        
        g_signal_connect (data->window,
                          "delete-event",
                          G_CALLBACK (window_delete_event_cb),
                          data);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (data->window), vbox);

        hbox = gtk_hbox_new (FALSE, 4);
        gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

        data->play_pause_button = gtk_toggle_button_new ();
        bbox = gtk_hbox_new (FALSE, 4);
        image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PLAY,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (bbox), image);
        image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PAUSE,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (bbox), image);
        gtk_container_add (GTK_CONTAINER (data->play_pause_button), bbox);
        gtk_box_pack_start (GTK_BOX (hbox),
                            data->play_pause_button, FALSE, FALSE, 0);
        g_signal_connect (data->play_pause_button,
                          "toggled",
                          G_CALLBACK (play_pause_button_toggled_cb),
                          data);


        data->previous_button = gtk_button_new ();
        image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PREVIOUS,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (data->previous_button), image);
        gtk_box_pack_start (GTK_BOX (hbox),
                            data->previous_button, FALSE, FALSE, 0);
        g_signal_connect (data->previous_button,
                          "clicked",
                          G_CALLBACK (previous_button_clicked_cb),
                          data);

        data->next_button = gtk_button_new ();
        image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_NEXT,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (data->next_button), image);
        gtk_box_pack_start (GTK_BOX (hbox),
                            data->next_button, FALSE, FALSE, 0);
        g_signal_connect (data->next_button,
                          "clicked",
                          G_CALLBACK (next_button_clicked_cb),
                          data);

        button = gtk_button_new ();
        image = gtk_image_new_from_stock (GTK_STOCK_REMOVE,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (button), image);
        gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (remove_song_button_clicked_cb),
                          data);


        button = gtk_button_new ();
        image = gtk_image_new_from_stock (GTK_STOCK_ADD,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (button), image);
        gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (add_song_button_clicked_cb),
                          data);

#if 0
        button = gtk_button_new ();
        image = gtk_image_new_from_stock (GTK_STOCK_OPEN,
                                          GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add (GTK_CONTAINER (button), image);
        gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (open_playlist_button_clicked_cb),
                          data);
#endif

        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_AUTOMATIC,
                                        GTK_POLICY_ALWAYS);
        gtk_scrolled_window_set_shadow_type
                                       (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_SHADOW_IN);
        gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

        data->tree_view = gtk_tree_view_new ();
        gtk_container_add (GTK_CONTAINER (scrolled_window), data->tree_view);

        g_signal_connect (data->tree_view,
                          "row-activated",
                          G_CALLBACK (row_activated_cb),
                          data);

        gtk_tree_selection_set_mode 
          (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->tree_view)),
           GTK_SELECTION_MULTIPLE);
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (data->tree_view),
                                           FALSE);
        gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (data->tree_view),
                                      TRUE);
        gtk_tree_view_set_reorderable (GTK_TREE_VIEW (data->tree_view),
                                       TRUE);
        gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (data->tree_view),
                                          TRUE);

        /**
         * Set up list store.
         **/
        data->list_store = gtk_list_store_new (3,
                                               G_TYPE_STRING,
                                               G_TYPE_STRING,
                                               G_TYPE_STRING);

        gtk_tree_view_set_model (GTK_TREE_VIEW (data->tree_view),
                                 GTK_TREE_MODEL (data->list_store));

        gtk_tree_view_insert_column_with_data_func
                (GTK_TREE_VIEW (data->tree_view),
                 -1, "Playing",
                 gtk_cell_renderer_pixbuf_new (),
                 (GtkTreeCellDataFunc) playing_cell_func,
                 data, NULL);
        gtk_tree_view_insert_column_with_data_func
                (GTK_TREE_VIEW (data->tree_view),
                 -1, "Song",
                 gtk_cell_renderer_text_new (),
                 text_cell_func,
                 NULL, NULL);

        /**
         * Nothing is playing yet.
         **/
        set_playing_row (data, NULL);

        /**
         * Show it all.
         **/
        gtk_widget_show_all (data->window);

        /**
         * Enter main loop.
         **/
        gtk_main ();

        /**
         * Cleanup.
         **/
        g_object_unref (data->tag_reader);
        g_object_unref (data->playlist_parser);
        g_object_unref (data->audio_player);

        if (data->playing_row)
                gtk_tree_row_reference_free (data->playing_row);

        gtk_widget_destroy (data->window);

        g_slice_free (AppData, data);

        return 0;
}
