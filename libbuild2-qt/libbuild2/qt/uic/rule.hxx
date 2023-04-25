#pragma once

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
        const exe&     uic;     // Uic compiler target.
      };
    }
  }
}