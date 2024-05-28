# libbuild2-qt

Qt compilers (`moc`, `rcc`, `uic`) build system module for `build2`.

@@ TODO: ref to documentation.

This module is part of the standard pre-installed `build2` modules and no
extra integration steps are required other than the `using` directive in your
`buildfile`.

For development build instructions see [`libbuild2-hello/README`][build].
Note that the tests require a host configuration (for the Qt compilers) and
the explicit specification of the Qt version to use. For example:

```
bdep init --empty

bdep config create @host ../libbuild2-qt-build/host/ --type host cc config.cxx=g++
bdep config create @module ../libbuild2-qt-build/module/ --type build2 cc config.config.load=~build2
bdep config create @target ../libbuild2-qt-build/target/ cc config.cxx=g++

bdep init @module -d libbuild2-qt/
bdep init @target -d libbuild2-qt-tests/ config.libbuild2_qt_tests.qt=6
```


@@ TODO Explain target types and how to use them. E.g. something like this:

        cxx{moc_*} is generated from hxx{} and should be compiled but can be
        included.

        moc{} is generated from cxx{} and must be included.

[build]: https://github.com/build2/libbuild2-hello
