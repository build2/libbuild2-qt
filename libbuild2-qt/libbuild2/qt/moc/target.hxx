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
      // A moc-generated C++ source file generated from a C++ header (e.g.,
      // foo.hxx -> moc_foo.cxx) which is typically compiled but can also be
      // included.
      //
      class LIBBUILD2_QT_SYMEXPORT moc_cxx: public cxx::cxx
      {
      public:
        moc_cxx (context& c, dir_path d, dir_path o, string n)
            : cxx::cxx (c, move (d), move (o), move (n))
        {
          dynamic_type = &static_type;
        }

      public:
        static const target_type static_type;
      };

      // A moc-generated C++ source file generated from a C++ source file
      // (e.g., foo.cxx -> foo.moc) which should be included (compilation can
      // be made to work but is not fully supported).
      //
      class LIBBUILD2_QT_SYMEXPORT moc_moc: public cxx::ixx
      {
      public:
        moc_moc (context& c, dir_path d, dir_path o, string n)
            : cxx::ixx (c, move (d), move (o), move (n))
        {
          dynamic_type = &static_type;
        }

      public:
        static const target_type static_type;
      };
    }
  }
}
