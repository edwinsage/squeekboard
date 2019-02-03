eekboard - an easy to use virtual keyboard toolkit
==================================================

eekboard is a virtual keyboard software package, including a set of
tools to implement desktop virtual keyboards.

Building
--------

### Dependencies

REQUIRED: GLib2, GTK, PangoCairo, libxklavier, libcroco
OPTIONAL: libXtst, at-spi2-core, IBus, Clutter, Clutter-Gtk, Python, Vala, gobject-introspection, libcanberra

### Build from git repo

```
$ git clone git://github.com/ueno/eekboard.git
$ cd eekboard
$ ./autogen.sh --prefix=/usr --enable-gtk-doc
$ make
$ sudo make install
```

### Build from tarball

```
$ ./configure --prefix=/usr
$ make
$ sudo make install
```

Running
-------

```
$ eekboard
$ eekboard -f # show/hide automatically based on focus-in/focus-out events
```

Even though `eekboard -f` watches a11y events by default, it currently
works better with IBus.  To use IBus, do:

```
$ gsettings set org.fedorahosted.eekboard focus-listener 'ibus'
```
