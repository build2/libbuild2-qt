#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

// @@ TMP Or rather include forward.hxx and move these to the .cxx?
//
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      extern const dir_path module_dir;       // qt.moc/
      extern const dir_path module_build_dir; // qt.moc/build/

      // Return true if the specified class of options should be passed to
      // moc. Valid option classes are `poptions`, `predefs`, and
      // `sys_hdr_dirs`. Each option class is associated with a variable named
      // `qt.moc.auto_<class>`.
      //
      bool
      pass_moc_opts (const scope&, const char* oc);

      bool
      pass_moc_opts (const target&, const char* oc);
    }
  }
}
