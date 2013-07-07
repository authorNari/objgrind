# Objgrind

TODO...

## Install

```zsh
% svn co svn://svn.valgrind.org/valgrind/trunk valgrind
% cd valgrind
% git clone https://github.com/authorNari/objgrind.git
% patch -p1 -d . < objgrind/valgrind.patch
% ./autogen.sh
% ./configure
% make && make check
% ./vg-in-place --tool=objgrind objgrind/tests/tiny_tests
```
