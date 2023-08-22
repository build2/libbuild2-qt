#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      // moc
      //
      extern const char moc_ext[] = "moc";
      const target_type moc::static_type
      {
        "moc",
        &cxx::cxx_inc::static_type,
        &target_factory<moc>,
        nullptr /* fixed_extension */,
        &target_extension_var<moc_ext>,
        &target_pattern_var<moc_ext>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };
    }
  }
}
