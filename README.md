# libhexes

## Description
_libhexes_ is a low-level terminal control library, including optimization. It's primary designed for games, being similar to graphical APIs like [LibSDL](https://www.libsdl.org/), with the use of buffers and blitting features.

## Primary Features
* Buffers, with blitting support between them.
* A double buffer system for screen updates.
* Truecolor support.
* Full Unicode support.
* Resize, focus, & restore (from job control) detection.
* Mouse support.
* Key modifier support.
* Title & icon control.
* Terminal flags, like cursor adjustment and reverse video.
* Terminal optimization.
* Easy to use API.

Some features may only be available on certain terminals. For best results, an [xterm](https://invisible-island.net/xterm/) compatible terminal is recommended, but should be functional on most. See the [Compatibility](#compatibility) notes for full details. 

## History/Motivation
_libhexes_ was designed after I had a spark to produce a terminal game. The [curses](https://en.wikipedia.org/wiki/Curses_%28programming_library%29) set of libraries was generally the usual way to go for this sort of thing. However, I found that its API to be lacking and, in my opinion, a bit of a mess.

I figured that I'll produce my own terminal control library, designed to be similar to a minimal 2D graphics library, primary with blitting and double-buffering. Although these features could be layered on to top of _curses_, as it had a design limitation of not supporting truecolor, I elected to build my own.

After a few months of work, my first version was ready.

## Compatibility
When `HexInit()` is called, _libhexes_ will attempt to load the locally installed [terminfo](http://invisible-island.net/ncurses/ncurses.faq.html#which_terminfo) database for the current terminal type, as determined by the _TERM_ environment variable. It will also perform detection to check for color & Unicode support, along with some terminal quirks.

It was been designed in mind with support for most modern Linux terminal emulators, primary ones that support _xterm_ control sequences, as well as the built-in VTs on _Linux 4.15_ (linux), _OpenBSD 6.4_ (VT220), and _NetBSD 8.0_ (VT100).

Due to the nature of terminals, it's not possible to find out exactly what the terminal is capable of. If you plan to make use of non-standard colors or attributes, I would suggest including options within your programs to allow users to select if these should be used. Using features that the user's terminal doesn't support could place it in an invalid state.

For special key handling, _libhexes_ depends on _terminfo_ for these. During development, I found that some _terminfo_ databases aren't always correct. When using special keys, I would recommend making a fallback to an alphanumeric key as they should always be supported.

As for Windows systems, I had created an early port to it however I quickly discovered its console outputting compatibilities were remarkably slow, making it unsuitable for any use of heavy animation. In future, I aim to produce an [SDL](https://www.libsdl.org/) port that'll run on Windows.

## Copyright
_libhexes_ is written by Richard Walmsley <richwalm+hexes@gmail.com> and is released under the [ISC license](https://www.isc.org/downloads/software-support-policy/isc-license/). Please see the included [LICENSE.txt](LICENSE.txt) for the full text.
