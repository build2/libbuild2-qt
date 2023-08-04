#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/rule.hxx>
#include <libbuild2/dyndep.hxx>

#include <libbuild2/qt/export.hxx>

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
        const uint64_t version; // qt.version
        const exe&     moc;     // Moc compiler target.
        const string&  csum;    // Moc compiler checksum.
      };

      class LIBBUILD2_QT_SYMEXPORT compile_rule: public simple_rule,
                                                 private virtual data,
                                                 private dyndep_rule
      {
      public:
        explicit
        compile_rule (data&& d): data (move (d)) {}

        virtual bool
        match (action, target&) const override
        {
          return false;
        }

        virtual bool
        match (action, target&, const string&, match_extra&) const override;

        virtual recipe
        apply (action, target&) const override;

        struct match_data;

        target_state
        perform_update (action, const target&, match_data&) const;
      };
    }
  }
}
