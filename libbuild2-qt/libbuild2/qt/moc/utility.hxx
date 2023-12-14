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
      // `qt.moc.auto_<class>`.
      //
      // @@ Make a template (impl utility.txx).
      //
      bool
      pass_moc_options (const scope&, const char* option_class);

      bool
      pass_moc_options (const target&, const char* option_class);

      // Register scope operation callbacks.
      //
      void
      register_operation_callbacks (scope&);
    }
  }
}
