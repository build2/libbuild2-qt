#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>

namespace build2
{
  namespace qt
  {
    // Move to qt/utility.hxx if/when need it in more than one place.
    //
    extern const dir_path module_dir; // qt/

    namespace moc
    {
      // To form the complete path do:
      //
      //   root.out_path () / root.root_extra->build_dir / X_dir
      //
      extern const dir_path module_dir;       // qt/moc/
      extern const dir_path module_build_dir; // qt/moc/build/

      // Return true if the specified class of options should be passed to
      // moc. Valid option classes are `poptions`, `predefs`, and
      // `sys_hdr_dirs`. Each option class is associated with a variable named
      // `qt.moc.auto_<class>`. T is either scope or target.
      //
      template <typename T>
      bool
      pass_moc_options (const T&, const char* option_class);

      // Scope operation callback that cleans up moc module sidebuilds.
      //
      // For now the only known case where build/qt/moc/ does not get removed
      // by the standard fsdir{} chain (i.e., when this callback is not
      // registered) is if we build, say, libbuild2-qt-tests/moc/ with auto
      // predefs enabled so that build/qt/moc/build/predefs.hxx is created,
      // then disabled auto predefs again before doing clean on
      // libbuild2-qt-build/target/libbuild2-qt-tests/.
      //
      target_state
      clean_sidebuilds (action, const scope& rs, const build2::dir&);
    }
  }
}

#include <libbuild2/qt/moc/utility.txx>
