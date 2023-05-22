#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

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
    }
  }
}
