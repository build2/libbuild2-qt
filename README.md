# libbuild2-qt

Qt compilers (moc, rcc, uic) build system module for `build2`.

For build instructions see [`libbuild2-hello/README`](https://github.com/build2/libbuild2-hello).

@@ TODO Explain target types and how to use them. E.g. something like this:

        cxx{moc_*} is generated from hxx{} and should be compiled but can be
        included.

        moc{} is generated from cxx{} and must be included.
