#pragma once

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
        const exe&     ctgt;    // Moc compiler target.
      };
    }
  }
}
