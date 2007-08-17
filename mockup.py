#! /usr/bin/python

import gobject, gtk

window = gtk.Window()
window.set_title("Music Player")
window.set_border_width (4)
box = gtk.VBox(False, 4)
window.add(box)

# Buttons
button_box = gtk.HBox (False, 4)
box.pack_start (button_box, expand=False, fill=False)

button = gtk.ToggleButton()
b = gtk.HBox(False, 0)
b.add(gtk.image_new_from_stock (gtk.STOCK_MEDIA_PLAY, gtk.ICON_SIZE_BUTTON))
b.add(gtk.image_new_from_stock (gtk.STOCK_MEDIA_PAUSE, gtk.ICON_SIZE_BUTTON))
button.add(b)
button_box.pack_start (button, expand=False, fill=False)

button = gtk.Button()
button.add (gtk.image_new_from_stock (gtk.STOCK_MEDIA_PREVIOUS, gtk.ICON_SIZE_BUTTON))
button_box.pack_start (button, expand=False, fill=False)

button = gtk.Button()
button.add (gtk.image_new_from_stock (gtk.STOCK_MEDIA_NEXT, gtk.ICON_SIZE_BUTTON))
button_box.pack_start (button, expand=False, fill=False)

button = gtk.Button(label="Add Songs")
button.set_image (gtk.image_new_from_stock(gtk.STOCK_ADD, gtk.ICON_SIZE_BUTTON))
button_box.pack_end (button, expand=False, fill=False)

# Current song
song_box = gtk.HBox (False, 4)
image = gtk.image_new_from_stock(gtk.STOCK_MISSING_IMAGE, gtk.ICON_SIZE_DIALOG)
song_box.pack_start (image, False, False)
l = gtk.Label()
l.set_markup("<big><b>Dreamy Days</b></big>\nRoots Manuva")
l.set_alignment (0.0, 0.0)
song_box.pack_start (l)
box.pack_start(song_box, expand=False, fill=False)

# Playlist
store = gtk.ListStore(gtk.gdk.Pixbuf, gobject.TYPE_STRING)
store.insert (0, (window.render_icon (gtk.STOCK_MEDIA_PLAY,gtk.ICON_SIZE_MENU), "<b>Dreamy Days</b>\nRoots Manuva"))
store.insert (1, (None, "Hadjaha"))

view = gtk.TreeView (store)
view.get_selection().set_mode(gtk.SELECTION_MULTIPLE)
view.set_headers_visible(False)
view.set_reorderable (True)
view.set_rubber_banding(True)

column = gtk.TreeViewColumn()
renderer = gtk.CellRendererPixbuf()
column.pack_start (renderer, False)
column.set_attributes (renderer, pixbuf=0)
renderer = gtk.CellRendererText()
column.pack_start (renderer, True)
column.set_attributes (renderer, markup=1)
view.append_column(column)

scrolled = gtk.ScrolledWindow()
scrolled.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
scrolled.add(view)
box.pack_start(scrolled, expand=True, fill=True)

window.show_all()
gtk.main()
