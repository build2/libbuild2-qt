#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      // moc
      //
      extern const char moc_ext[] = "moc";
      const target_type moc::static_type
      {
        "moc",
        &cxx::cxx_inc::static_type,
        &target_factory<moc>,
        nullptr /* fixed_extension */,
        &target_extension_var<moc_ext>,
        &target_pattern_var<moc_ext>,
        nullptr /* print */,
        &file_search,
        target_type::flag::none
      };

      // automoc
      //
      group_view automoc::
      group_members (action a) const
      {
        if (members_on == 0) // Not yet discovered.
          return group_view {nullptr, 0};

        // Members discovered during anything other than perform_update are
        // only good for that operation.
        //
        // We also re-discover the members on each update and clean not to
        // overcomplicate the already complicated automoc_rule::apply() logic.
        //
        if (members_on != ctx.current_on)
        {
          if (members_action != perform_update_id ||
              a == perform_update_id ||
              a == perform_clean_id)
            return group_view {nullptr, 0};
        }

        // Note that we may have no members (e.g., perform_configure and there
        // are no static members). However, whether std::vector returns a
        // non-NULL pointer in this case is undefined.
        //
        size_t n (members.size ());
        return group_view {
          n != 0
          ? reinterpret_cast<const target* const*> (members.data ())
          : reinterpret_cast<const target* const*> (this),
          n};
      }

      const target_type automoc::static_type
      {
        "automoc",
        &target::static_type,
        &target_factory<automoc>,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &target_search,
        //
        // Group with the "see through" iteration and dynamic members.
        //
        target_type::flag::see_through | target_type::flag::dyn_members
      };
    }
  }
}
