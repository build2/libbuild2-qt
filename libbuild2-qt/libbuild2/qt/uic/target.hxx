#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/target.hxx>

#include <libbuild2/qt/export.hxx>

namespace build2
{
  namespace qt
  {
    namespace uic
    {
      class LIBBUILD2_QT_SYMEXPORT ui: public file
      {
      public:
        ui (context& c, dir_path d, dir_path o, string n)
            : file (c, move (d), move (o), move (n))
        {
          dynamic_type = &static_type;
        }

      public:
        static const target_type static_type;
      };
    }
  }
}
