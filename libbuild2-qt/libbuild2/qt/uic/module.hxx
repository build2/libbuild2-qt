#pragma once

#include <libbuild2/module.hxx>

#include <libbuild2/qt/uic/rule.hxx>

namespace build2
{
  namespace qt
  {
    namespace uic
    {
      class module: public build2::module, public virtual data
      {
      public:
        explicit module (data&& d) : data (move (d)) {}
      };
    }
  }
}
