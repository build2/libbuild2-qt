#pragma once

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/cxx/target.hxx>

#include <libbuild2/qt/export.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      // A moc-generated C++ source file generated from a C++ source file
      // (e.g., foo.cxx -> foo.moc) which should be included.
      //
      class LIBBUILD2_QT_SYMEXPORT moc: public cxx::cxx_inc
      {
      public:
        moc (context& c, dir_path d, dir_path o, string n)
            : cxx::cxx_inc (c, move (d), move (o), move (n))
        {
          dynamic_type = &static_type;
        }

      public:
        static const target_type static_type;
      };
    }
  }
}
