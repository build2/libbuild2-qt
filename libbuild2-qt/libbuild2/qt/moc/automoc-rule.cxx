#include <libbuild2/qt/moc/automoc-rule.hxx>

#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/diagnostics.hxx>

#include <libbuild2/bin/target.hxx>

#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      bool automoc_rule::
      match (action a, target& t) const
      {
        tracer trace ("qt::moc::automoc_rule::match");

        for (prerequisite_member p: group_prerequisite_members (a, t))
        {
          // If excluded or ad hoc, then don't factor it into our tests.
          //
          if (include (a, t, p) == include_type::normal)
            if (p.is_a<cxx> () || p.is_a<hxx> ())
              return true;
        }

        l4 ([&]{trace << "no header or source file for target " << t;});
        return false;
      }

      recipe automoc_rule::
      apply (action a, target& xt) const
      {
        tracer trace ("qt::moc::automoc_rule::apply");

        automoc& g (xt.as<automoc> ());
        context& ctx (g.ctx);

        // Match prerequisites.
        //
        // @@ TODO If we could do something like match_direct_async() here we
        //         wouldn't have to execute any prereqs in perform() because
        //         it would be delegated to the moc compile rule. As it stands
        //         we are executing all prereqs, even the ones that don't need
        //         to get compiled by moc.
        //
        match_prerequisite_members (a, g);

        auto& pts (g.prerequisite_targets[a]);
        vector<prerequisite> lib_prereqs;

        // Update header and source file prerequisites now to ensure they all
        // exist before we do the moc scan.
        //
        // But first mark header and source file prerequisites with
        // include_udm (for update_during_match_prerequisites()) and collect
        // library prerequisites while we're at it.
        //
        for (prerequisite_target& pt: pts)
        {
          using namespace bin;

          if (pt->is_a<hxx> () || pt->is_a<cxx> ())
          {
            pt.include |= prerequisite_target::include_udm;
          }
          else if (pt->is_a<libs>  ()  ||
                   pt->is_a<liba>  ()  ||
                   pt->is_a<libul> ()  ||
                   pt->is_a<libux> ())
          {
            lib_prereqs.emplace_back (*pt);
          }
        }

        update_during_match_prerequisites (trace, a, g);

        // Discover group members (moc outputs).
        //
        // @@ TODO Scan prerequisites for Qt meta-object macros and make
        //         members only for those that match.
        //
        //         For the time being we simply make members for all
        //         prerequisites.
        //
        g.reset_members (a);

        for (const prerequisite_target& pt: pts)
        {
          // Derive the moc output name and target type.
          //
          string mn;              // Member target name.
          const target_type* mtt; // Member target type.

          if (pt->is_a<hxx> ())
          {
            mn = string ("moc_") + pt->name;
            mtt = &cxx::static_type;
          }
          else if (pt->is_a<cxx> ())
          {
            mn = pt->name;
            mtt = &moc::static_type;
          }
          else
            continue; // Not a header or source file.

          // Search for an existing target or create a new one.
          //
          // @@ TMP I see pt->out always works (if it's empty them pt->dir is
          //        automatically taken) but it seems safer to take care of it
          //        here instead.
          //
          dir_path md (pt->out.empty() ? pt->dir : pt->out); // Member directory.

          // Pass empty out path because members are always in out; pass null
          // scope because target paths are always absolute.
          //
          pair<target&, ulock> tl (search_new_locked (ctx,
                                                      *mtt,
                                                      md,          // dir
                                                      dir_path (), // out
                                                      mn,
                                                      nullptr,     // ext
                                                      nullptr));   // scope

          cc& m (tl.first.as<cc> ()); // Member target.

          if (tl.second) // Locked, so a new target was created.
          {
            tl.second.unlock ();
            m.derive_path_with_extension (m.derive_extension ());
          }
          else
          {
            // Fail if the member has already been matched (in which case the
            // lock will fail to be acquired). This shouldn't normally happen
            // since we are the only ones that should know about this target
            // (otherwise why is it dynamicaly discovered). However, nothing
            // prevents the user from depending on such a target, however
            // misguided.
            //
            target_lock l (lock (a, m));

            if (!l)
              fail << "group " << g << " member " << m << " is already matched" <<
                info << "dynamically extracted group members cannot be used as "
                     << "prerequisites directly, only via group";

            if (m.group != nullptr)
              fail << "group " << g << " member " << m
                   << " is already member of group " << *m.group;
          }

          // Set the member's prerequisites: the header or source file, and
          // all of the library prerequisites.
          //
          {
            prerequisites ps {prerequisite (*pt.target)};
            for (const prerequisite& l: lib_prereqs)
              ps.push_back (l);

            m.prerequisites (move (ps));
          }

          // Match the member.
          //
          // @@ TMP Matching with match_direct_sync() instead of
          //        match_members() so that the members' dependent counts are
          //        not incremented. Otherwise it causes problems during
          //        clean: the members are postponed, but because the
          //        see-through automoc{} is never executed, the members never
          //        get executed either.
          //
          //        Matching using match_members() just happens to not break
          //        because in the `exe{}: automoc{} cxx{}` setup the moc
          //        outputs get updated during match (header extraction) which
          //        calls execute_direct_sync which bypasses the usual
          //        dependents count and execution order. When updating the
          //        automoc{} directly the "unexecuted matched targets"
          //        failure does get triggered.
          //
          // @@ TODO Matching 1000s of members sync is probably not good.
          //
          match_direct_sync (a, m);

          g.members.push_back (&m);
        }

        return [this] (action a, const target& t)
        {
          return perform (a, t);
        };
      }

      target_state automoc_rule::
      perform (action a, const target& xt)
      {
        const automoc& g (xt.as<automoc> ());
        context& ctx (g.ctx);

        // Update prerequisites.
        //
        // Note that this is done purely to keep the dependency counts
        // straight; all decisions are delegated to the moc compile rule.
        //
        // @@ TODO Most likely we'll be doing this on many prereqs that don't
        //    need to be moc'ed, which is not great, but we have to because we
        //    do match_prerequisites() in apply(). If we could do something
        //    like match_direct_prerequisites() there then we wouldn't need to
        //    execute any prereqs here.
        //
        execute_prerequisites (a, g);

        target_state r (target_state::unchanged);

        // Execute members asynchronously.
        //
        // This is basically execute_members(), but based on
        // execute_direct_async() to complement our use of match_direct_sync()
        // above.
        //
        size_t busy (ctx.count_busy ());
        atomic_count& tc (g[a].task_count);

        wait_guard wg (ctx, busy, tc);

        if (ctx.current_mode == execution_mode::first) // Straight
        {
          for (const cc* m: g.members)
            execute_direct_async (a, *m, busy, tc);

          wg.wait ();

          for (const cc* m: g.members)
            r |= execute_complete (a, *m);
        }
        else // Reverse
        {
          for (size_t i (g.members.size ()); i != 0;)
            execute_direct_async (a, *g.members[--i], busy, tc);

          wg.wait ();

          for (size_t i (g.members.size ()); i != 0;)
            r |= execute_complete (a, *g.members[--i]);
        }

        return r;
      }
    }
  }
}
