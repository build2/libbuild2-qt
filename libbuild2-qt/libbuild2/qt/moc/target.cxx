#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      static optional<string>
      moc_default_ext (const target_key& tk,
                       const scope& s,
                       const char*,
                       bool);

      // moc
      //
      // @@ TODO Derive from file instead of ixx.
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

      // @@ TODO Remove this function once moc{} is derived from file{}
      //    (replace it with target_extension_var<moc_ext>()).
      //
      static optional<string>
      moc_default_ext (const target_key& tk,
                       const scope& s,
                       const char*,
                       bool)
      {
        // @@ TMP target_extension_var() returns the base type's `extension`
        //        value if not set on moc{*} so we get "ixx" instead of "moc"
        //        if moc{*}'s extension var is unset. This function works
        //        around that but the method is most probably not correct so
        //        did not try to implement this properly.
        //
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
