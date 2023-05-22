#pragma once

#include <libbuild2/types.hxx>   // @@ TODO: make sure in all headers.
#include <libbuild2/utility.hxx>

#include <libbuild2/module.hxx>

#include <libbuild2/qt/rcc/rule.hxx>

namespace build2
{
  namespace qt
  {
    namespace rcc
    {
      class module: public build2::module,
                    public virtual data,
                    public compile_rule
      {
      public:
        explicit module (data&& d): data (move (d)), compile_rule (move (d)) {}
      };
    }
  }
}
