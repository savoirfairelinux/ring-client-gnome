# Jami client Gnome

[![Build Status](https://jenkins.jami.net/buildStatus/icon?job=client-gnome)](https://jenkins.jami.net/job/client-gnome/)

Jami-client-gnome is a Jami client written in GTK+3. It uses libRingClient to
communicate with the Jami daemon and for all of the underlying models and their
logic. Ideally Jami-client-gnome should only contain UI related code and any
wrappers necessary for interacting with libRingClient.

Packages for Debian/Ubuntu/Fedora can be found at https://jami.net/

More info about the Jami project and the clients can be found on our Gitlab's instance:
https://git.jami.net/

GNU Jami welcomes contribution from everyone. See [CONTRIBUTING.md](CONTRIBUTING.md) for help getting started.

# Setting up your environment

## Requirements

- Jami daemon
- libRingClient
- GTK+3 (3.10 or higher)
- Qt5 Core
- X11
- gnome-icon-theme-symbolic (certain icons are used which other themes might be missing)
- A font with symbols U+1F4DE and U+1F57D (used in some messages)
- libnotify (optional, if you wish to receive desktop notifications of incoming calls, etc)
- gettext (optional to compile translations)

On Debian/Ubuntu these can be installed by:
```bash
sudo apt-get install g++ cmake libgtk-3-dev qtbase5-dev libclutter-gtk-1.0-dev gnome-icon-theme-symbolic libnotify-dev gettext
```

On Fedora:
```bash
sudo dnf install gcc-c++ cmake gtk3-devel qt5-qtbase-devel clutter-gtk-devel gnome-icon-theme-symbolic libnotify-devel gettext
```

The build instructions for the daemon and libRingClient can be found in their
respective repositories. See Gerrit:
 - https://gerrit-ring.savoirfairelinux.com/#/admin/projects/


## Compiling

In the project root dir:
```bash
mkdir build
cd build
cmake ..
make
```

You can then simply run `./jami-gnome` from the build directory

## Installing

If you're building the client for use (rather than testing of packaging), it is
recommended that you install it on your system, eg: in `/usr`, `/usr/local`, or
`/opt`, depending on your distro's preference to get full functionality such as
desktop integration. In this case you should perform a 'make install' after
building the client.


## Building without installing Jami daemon and libRingClient

It is possible to build ring-client-gnome without installing the daemon and
libRingClient on your system (eg: in `/usr` or `/usr/local`):

1. build the daemon
2. when building libRingClient, specify the location of the daemon lib in the
   cmake options with -DRING_BUILD_DIR=, eg:
   `-DRING_BUILD_DIR=/home/user/ring/daemon/src`
3. to get the proper headers, we still need to 'make install' libRingClient, but
   we don't have to install it in /usr, so just specify another location for the
   install prefix in the cmake options, eg:
   `-DCMAKE_INSTALL_PREFIX=/home/user/ringinstall`
4. now compile libRingClient and do 'make install', everything will be installed
   in the directory specified by the prefix
4. now we just have to point the client to the libRingClient cmake module during
   the configuration:
   `-DLibRingClient_DIR=/home/user/ringinstall/lib/cmake/LibRingClient`


## Debugging

For now, the build type of the client is "Debug" by default, however it is
useful to also have the debug symbols of libRingClient. To do this, specify this
when compiling libRingClient with `-DCMAKE_BUILD_TYPE=Debug` in the cmake
options.

## Generating marshals.*

```
glib-genmarshal --header marshals.list > marshals.h
glib-genmarshal --include-header=marshals.h --body marshals.list > marshals.cpp
```