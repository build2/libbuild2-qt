#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/rule.hxx>
#include <libbuild2/dyndep.hxx>

#include <libbuild2/c/target.hxx>    // h{} (fallback)
#include <libbuild2/cc/module.hxx>   // cc::module
#include <libbuild2/cxx/target.hxx>

#include <libbuild2/qt/export.hxx>

#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      // Cached data shared between rules and the module.
      //
      struct data
      {
        const uint64_t    version; // qt.version
        const exe&        ctgt;    // Moc compiler target.
        const string&     csum;    // Moc compiler checksum.
        const cc::module* cxx_mod; // The cxx module.
      };

      class LIBBUILD2_QT_SYMEXPORT compile_rule: public rule,
                                                 private virtual data,
                                                 private dyndep_rule
      {
      public:
        explicit
        compile_rule (data&& d): data (move (d)) {}

        virtual bool
        match (action, target&, const string&, match_extra&) const override;

        virtual recipe
        apply (action, target&, match_extra&) const override;

        struct match_data;

        target_state
        perform_update (action, const target&, match_data&) const;

        using h   = build2::c::h;
        using cxx = build2::cxx::cxx;
        using hxx = build2::cxx::hxx;
        using moc = qt::moc::moc;
      };
    }
  }
}
