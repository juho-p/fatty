FaTTY
-----

FaTTY is the [Cygwin](http://cygwin.com) Terminal emulator with tabs. It is
useful for Windows and Cygwin users who want powerful terminal.

FaTTY is based on [mintty](https://github.com/mintty/mintty). The main
difference to mintty is that you can run multiple session in single window
using tabs.

* Most features from mintty should work
* To create new tab, press ctrl-shift-T
* To change active tab, click it with mouse or press shift-(left arrow|right arrow)
* To move tab, press ctrl-shift-(arrow direction)

If you find bugs (there are probably many), you may report them on Github or
send pull requests

###Installing

To install, run cygwin setup*.exe and have at least following packages marked for
install:

* gcc-g++
* make
* win32api-headers
* git

Then, run in Cygwin terminal run following commands:

    git clone https://github.com/juho-p/fatty.git
    cd fatty
    make
    cp src/fatty.exe /bin
  
  You can then try running it by typing `fatty`
  
  Then you probably want to create shortcut to your Windows desktop to run fatty.
  After that, you have decent terminal with tabs for your Cygwin!
