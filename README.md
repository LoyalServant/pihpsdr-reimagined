# pihpsdr

This is a windows AND GTK4 port.
this was originally forked from DL1YCF's wonderful fork here:
https://github.com/dl1ycf/pihpsdr
If you want something stable that runs on a raspberry pi I suggest you use his.
I did this for my own personal use in a hermes "maestro" like device.
This is super super alpha code... and may not work as you expect.

What radios work?
- Hermes Lite
what is probably broken:
- everything else.

***

New protocol discovery and new protocol related bits are pretty much guaranteed to not work.
I don't have the radios to test everything. All I have is the Hermes Lite, and that was my target use for this.

There is a script, 'copydlls' that sorts out dependencies for Windows.

***
Other notes on building...

It's doubtful this builds on Apple boxes. I don't have one.
It will build in MSYS2, and on Linux.
It pretty much runs out of it's build directory - there is no install.
I run this on an Orange Pi 5 Plus, and Windows 11.
When running on the Orange Pi - gdm3 interferes with it in ways I don't understand so I suggest you use weston.
my weston.ini looks like this:

[core]
modules=xwayland.so

[shell]
panel-location=""
panel-position=none

I have my orange pi set up to be a "kiosk" so the user logs in automatically, starts weston, and starts pihpsdr.
There is a lot more that can be done in weston, but this minimal setup works for me.

***

Features of this very very alpha code....
- runs on Windows? :)
- GTK4 and thus gestures.
- consumes less resources (at least on my hardware) because GTK4 uses the GPU.

