#include <libbuild2/qt/rcc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace rcc
    {
      extern const char qrc_ext[] = "qrc";
      const target_type qrc::static_type
      {
        "qrc",
        &file::static_type,
        &target_factory<qrc>,
        &target_extension_fix<qrc_ext>,
        nullptr, /* default_extension */
        &target_pattern_fix<qrc_ext>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };
    }
  }
}
