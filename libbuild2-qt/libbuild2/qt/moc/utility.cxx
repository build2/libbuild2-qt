#include <libbuild2/qt/moc/utility.hxx>

#include <libbuild2/filesystem.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      const dir_path module_dir ("qt.moc");
      const dir_path module_build_dir (dir_path (module_dir) /= "build");

      template <typename T>
      static bool
      pass_moc_opts_impl (const T& t, const char* oc)
      {
        // Fall back to qt.moc.auto_preprocessor if the variable is null or
        // undefined.
        //
        lookup l (t[string ("qt.moc.auto_") + oc]);
        return l ? cast<bool> (l)
                 : cast_true<bool> (t["qt.moc.auto_preprocessor"]);
      }

      bool
      pass_moc_opts (const scope& s, const char* oc)
      {
        return pass_moc_opts_impl (s, oc);
      }

      bool
      pass_moc_opts (const target& t, const char* oc)
      {
        return pass_moc_opts_impl (t, oc);
      }

      // Scope operation callback that cleans up moc module sidebuilds.
      //
      // @@ TMP The only case I could find where build/qt.moc/ does not get
      //        removed by the standard fsdir{} chain (ie, when this callback
      //        is not registered) was if I built, say,
      //        libbuild2-qt-tests/moc/ with auto predefs enabled so that
      //        build/qt.moc/build/moc_predefs.hxx is created, then disabled
      //        auto predefs again before running b clean on
      //        libbuild2-qt-build/target/libbuild2-qt-tests/. Are there other
      //        cases?
      //
      static target_state
      clean_sidebuilds (action, const scope& rs, const build2::dir&)
      {
        context& ctx (rs.ctx);

        const dir_path& out_root (rs.out_path ());

        dir_path d (out_root / rs.root_extra->build_dir / module_build_dir);

        if (exists (d))
        {
          if (rmdir_r (ctx, d))
          {
            // Clean up qt.moc/ if it became empty.
            //
            d = out_root / rs.root_extra->build_dir / module_dir;
            if (empty (d))
            {
              rmdir (ctx, d, 2);

              // And build/ if it also became empty (e.g., in case of a build
              // with a transient configuration).
              //
              d = out_root / rs.root_extra->build_dir;
              if (empty (d))
                rmdir (ctx, d, 2);
            }

            return target_state::changed;
          }
        }

        return target_state::unchanged;
      }

      void
      register_op_callbacks (scope& s)
      {
        // It feels natural to clean up sidebuilds as a post operation but
        // that prevents the (otherwise-empty) out root directory to be
        // cleaned up (via the standard fsdir{} chain).
        //
        s.operation_callbacks.emplace (
            perform_clean_id,
            scope::operation_callback {&clean_sidebuilds, nullptr /*post*/});
      }
    }
  }
}
