#pragma once

#include <libbuild2/module.hxx>

#include <libbuild2/qt/moc/rule.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      class module: public build2::module, virtual data
      {
      public:
        explicit module (data&& d) : data (move (d)) {}
      };
    }
  }
}
