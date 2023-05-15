#pragma once

namespace build2
{
  namespace qt
  {
    namespace rcc
    {
      // Cached data shared between rules and the module.
      //
      struct data
      {
        const uint64_t version; // qt.version
        const exe&     rcc;     // Rcc compiler target.
      };
    }
  }
}
