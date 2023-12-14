#include <libbuild2/qt/moc/utility.hxx>

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
    }
  }
}
