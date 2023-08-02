#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      // moc.cxx
      //
      const target_type moc_cxx::static_type
      {
        "moc.cxx",
        &cxx::cxx::static_type,
        &target_factory<moc_cxx>,
        cxx::cxx::static_type.fixed_extension,
        cxx::cxx::static_type.default_extension,
        cxx::cxx::static_type.pattern,
        nullptr /* print */,    // Like cxx::cxx.
        &file_search,           // Like cxx::cxx.
        target_type::flag::none // Like cxx::cxx.
      };

      // @@ TMP Without using this function for moc_moc::default_extension, b
      //        {clean update}(libbuild2-qt-tests/) gives the following error:
      //
      //          error: multiple targets share path ../libbuild2-qt-build/target/libbuild2-qt-tests/moc/sink.moc
      //          info: first target:  ../libbuild2-qt-build/target/libbuild2-qt-tests/moc/moc.moc{sink}
      //          info: second target: ../libbuild2-qt-build/target/libbuild2-qt-tests/moc/h{sink}
      //          info: target ../libbuild2-qt-build/target/libbuild2-qt-tests/moc/moc.moc{sink} has non-noop recipe
      //          info: target ../libbuild2-qt-build/target/libbuild2-qt-tests/moc/h{sink} is not declared in a buildfile
      //          info: perhaps it is a dynamic dependency?
      //
      //       Might be because dyndep::match_extension() calls
      //       default_extension() only, and not fixed_extension().
      //
      template <const char* def>
      optional<string>
      moc_default_extension (const target_key&,
                             const scope&,
                             const char*,
                             bool)
      {
        return def;
      }

      // moc.moc
      //
      extern const char moc_ext[] = "moc";
      const target_type moc_moc::static_type
      {
        "moc.moc",
        &cxx::ixx::static_type,
        &target_factory<moc_moc>,
        &target_extension_fix<moc_ext>,
        &moc_default_extension<moc_ext>,
        &target_pattern_fix<moc_ext>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };
    }
  }
}
