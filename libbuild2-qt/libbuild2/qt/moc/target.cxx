#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {

#if 0
      extern const char rs_ext_def[] = "rs";
      const target_type rs::static_type
      {
        "rs",
        &file::static_type,
        &target_factory<rs>,
        nullptr /* fixed_extension */,
        &target_extension_var<rs_ext_def>,
        &target_pattern_var<rs_ext_def>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };
#endif

      static optional<string>
      moc_default_ext (const target_key& tk,
                       const scope& s,
                       const char*,
                       bool);

      // moc
      //
      extern const char moc_ext[] = "moc";
      const target_type moc::static_type
      {
        "moc",
        &cxx::ixx::static_type,
        &target_factory<moc>,
        nullptr /* fixed_extension */,
        &moc_default_ext,
        &target_pattern_var<moc_ext>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };

      // @@ TMP target_extension_var() returns the base type's `extension`
      //        value if not set on moc{*} so we get "ixx" instead of "moc" if
      //        moc{*}'s extension var is unset. This function works around
      //        that but the method is most probably not correct so did not
      //        try to implement this properly.
      //
      static optional<string>
      moc_default_ext (const target_key& tk,
                       const scope& s,
                       const char*,
                       bool)
      {
        optional<string> e (
            target_extension_var<moc_ext> (tk, s, nullptr, false));

        if (e && e != moc_ext)
        {
          // Compare e with the base's default extension. If they match,
          // return moc_ext, otherwise it must be moc{*}'s extension value so
          // return that.
          //
          auto base_default_ext = moc::static_type.base->default_extension;

          if (base_default_ext != nullptr)
          {
            string bn (tk.type->base->name);
            target_key btk {tk.type->base, nullptr, nullptr, &bn, nullopt};

            if (e != base_default_ext (btk, s, nullptr, false))
              return e;
          }
        }

        return moc_ext;
      }
    }
  }
}
