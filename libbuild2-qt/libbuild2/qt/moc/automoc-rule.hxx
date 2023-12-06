#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/rule.hxx>

#include <libbuild2/c/target.hxx>
#include <libbuild2/cxx/target.hxx>

#include <libbuild2/qt/export.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      // Scan an automoc{} target's prerequisite header and source files for
      // the presence of Qt meta-object macros, create moc output targets for
      // those that match, and delegate updating them to the qt.moc.compile
      // rule.
      //
      class LIBBUILD2_QT_SYMEXPORT automoc_rule: public simple_rule
      {
      public:
        automoc_rule (): rule_id_ ("qt.moc.automoc 1") {}

        virtual bool
        match (action, target&) const override;

        virtual recipe
        apply (action, target&) const override;

        static target_state
        perform (action, const target&);

      private:
        const char* rule_id_;

        using cc  = build2::cc::cc;
        using h   = build2::c::h;
        using cxx = build2::cxx::cxx;
        using hxx = build2::cxx::hxx;
      };
    }
  }
}
