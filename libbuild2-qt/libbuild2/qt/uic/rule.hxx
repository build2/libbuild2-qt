#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/rule.hxx>

#include <libbuild2/qt/export.hxx>

namespace build2
{
  namespace qt
  {
    namespace uic
    {
      // Cached data shared between rules and the module.
      //
      struct data
      {
        const uint64_t version; // qt.version
        const exe*     ctgt;    // Uic compiler target (NULL if load-only).
        const string&  csum;    // Uic compiler checksum.
      };

      class LIBBUILD2_QT_SYMEXPORT compile_rule: public simple_rule,
                                                 private virtual data
      {
      public:
        explicit
        compile_rule (data&& d): data (move (d)) {}

        virtual bool
        match (action, target&) const override;

        virtual recipe
        apply (action, target&) const override;

        target_state
        perform_update (action, const target&) const;
      };
    }
  }
}
