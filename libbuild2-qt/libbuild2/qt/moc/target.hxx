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

      // A see-through group which is dynamically populated with a cxx{moc_*}
      // or moc{} target member for each hxx{} or cxx{} prerequisite that
      // needs to be moc'd.
      //
      class LIBBUILD2_QT_SYMEXPORT automoc: public mtime_target
      {
      public:
        vector<const target*> members; // Layout compatible with group_view.
        action members_action; // Action on which members were resolved.
        size_t members_on = 0; // Operation number on which members were resolved.

        void
        reset_members (action a)
        {
          members.clear ();
          members_action = a;
          members_on = ctx.current_on;
        }

        automoc (context& c, dir_path d, dir_path o, string n)
            : mtime_target (c, move (d), move (o), move (n))
        {
          dynamic_type = &static_type;
        }

        virtual group_view
        group_members (action) const override;

      public:
        static const target_type static_type;
      };
    }
  }
}
