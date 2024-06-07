#include <libbuild2/qt/moc/utility.hxx>

#include <libbuild2/filesystem.hxx>

namespace build2
{
  namespace qt
  {
    const dir_path module_dir ("qt");

    namespace moc
    {
      const dir_path module_dir (dir_path (qt::module_dir) /= "moc");
      const dir_path module_build_dir (dir_path (module_dir) /= "build");

      target_state
      clean_sidebuilds (action, const scope& rs, const build2::dir&)
      {
        // NOTE: see also the auto_predefs() lambda in compile_rule::apply().

        context& ctx (rs.ctx);

        const dir_path& out_root (rs.out_path ());

        dir_path d (out_root / rs.root_extra->build_dir / module_build_dir);

        if (exists (d))
        {
          if (rmdir_r (ctx, d))
          {
            // Clean up moc/ if it became empty.
            //
            d = out_root / rs.root_extra->build_dir / module_dir;
            if (empty (d))
            {
              rmdir (ctx, d, 2);

              // Clean up qt/ if it became empty.
              //
              d = out_root / rs.root_extra->build_dir / qt::module_dir;
              if (empty (d))
              {
                rmdir (ctx, d, 2);

                // And build/ if it also became empty (e.g., in case of a
                // build with a transient configuration).
                //
                d = out_root / rs.root_extra->build_dir;
                if (empty (d))
                  rmdir (ctx, d, 2);
              }
            }

            return target_state::changed;
          }
        }

        return target_state::unchanged;
      }
    }
  }
}
