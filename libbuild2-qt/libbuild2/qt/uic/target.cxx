#include <libbuild2/qt/uic/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace uic
    {
      extern const char ui_ext[] = "ui";
      const target_type ui::static_type
      {
        "ui",
        &file::static_type,
        &target_factory<ui>,
        &target_extension_fix<ui_ext>,
        nullptr, /* default_extension */
        &target_pattern_fix<ui_ext>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };
    }
  }
}
