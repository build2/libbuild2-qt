#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/module.hxx>

#include <libbuild2/qt/moc/rule.hxx>
#include <libbuild2/qt/moc/automoc-rule.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      class module: public build2::module,
                    public virtual data,
                    public compile_rule,
                    public automoc_rule
      {
      public:
        explicit module (data&& d): data (move (d)), compile_rule (move (d)) {}
      };
    }
  }
}
