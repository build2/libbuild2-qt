#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/module.hxx>

#include <libbuild2/qt/export.hxx>

namespace build2
{
  namespace qt
  {
    //-
    // Module `qt` does not require bootstrapping.
    //
    // Submodules:
    //
    //   `qt.moc.guess`  -- set variables describing the moc compiler.
    //   `qt.moc.config` -- load `qt.moc.guess` and set the rest of the variables.
    //   `qt.moc`        -- load `qt.moc.config` and register targets and rules.
    //
    //   `qt.rcc.guess`  -- set variables describing the rcc compiler.
    //   `qt.rcc.config` -- load `qt.rcc.guess` and set the rest of the variables.
    //   `qt.rcc`        -- load `qt.rcc.config` and register targets and rules.
    //
    //   `qt.uic.guess`  -- set variables describing the uic compiler.
    //   `qt.uic.config` -- load `qt.uic.guess` and set the rest of the variables.
    //   `qt.uic`        -- load `qt.uic.config` and register targets and rules.
    //
    //   `qt`            -- load the `qt.moc`, `qt.rcc`, and `qt.uic` submodules.
    //
    // Each of the `qt.{moc,rcc,uic}` modules split the configuration process
    // into two parts: guessing the compiler information and the actual
    // configuration. This allows adjusting configuration based on the
    // compiler information by first loading the guess module.
    //
    // Note that only the qt.*.guess modules support optional loading. In the
    // unlikely case that optional loading is needed, one must first load the
    // qt.*.guess module, check if successful, then load the rest.
    //
    //-
    extern "C" LIBBUILD2_QT_SYMEXPORT const module_functions*
    build2_qt_load ();
  }
}
