#pragma once

#include <libbuild2/module.hxx>

namespace build2
{
  namespace qt
  {
    class module_moc: public build2::module
    {
    public:
      module_moc () {}
    };

    class module_rcc: public build2::module
    {
    public:
      module_rcc () {}
    };

    class module_uic: public build2::module
    {
    public:
      module_uic () {}
    };
  }
}
