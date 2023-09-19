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
      // and/or moc{} target members for each hxx{} or cxx{} prerequisite that
      // needs to be compiled by moc.
      //
      // Note that this group implements an "inverse" semantics. Normally,
      // updating a group's members will delegate to the group recipe (via
      // special group_recipe). Here, however, we do the opposite: updating
      // the group causes updating each of its members individually. So, in a
      // sense, this group is a special kind of alias for its members.
      //
      class LIBBUILD2_QT_SYMEXPORT automoc: public mtime_target // @@ target
      {
      public:
        // @@ Use cc instead of target.
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
