#pragma once

#include <libbuild2/module.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      class module: public build2::module
      {
      public:
        module () {}
      };
    }
  }
}
