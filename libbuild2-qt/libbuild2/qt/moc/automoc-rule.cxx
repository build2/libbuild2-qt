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
          {
            if (p.is_a<cxx> () || p.is_a<hxx> ())
              return true;
          }
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

        // The overall plan is as follows:
        //
        // 1. Match and update sources and headers (we need to update because
        //    we scan them), and collect all the library prerequisites
        //    (because we need to propagate them to prerequisites of
        //    dependencies that we synthesize).
        //
        // 2. Scan sources and headers for meta-object macros. Those that
        //    contain such macros are made members of this group and for them
        //    we synthesize a dependency and match the moc compile rule.

        // Match and update header and source file prerequisites and collect
        // library prerequisites.
        //
        auto& pts (g.prerequisite_targets[a]);
        vector<prerequisite> lib_prereqs;
        {
          // Wait with unlocked phase to allow phase switching.
          //
          wait_guard wg (ctx, ctx.count_busy (), g[a].task_count, true);

          for (const prerequisite_member& p: group_prerequisite_members (a, g))
          {
            using namespace bin;

            include_type pi (include (a, g, p));

            // @@ TMP Not sure what exactly the user's mistake would be here.
            //
            // Fail if there are any ad hoc prerequisites because perform is
            // not usually executed.
            //
            if (pi == include_type::adhoc)
              fail << "ad hoc prerequisite " << p << " of target " << g
                   << "will not be updated";

            // Ignore excluded.
            //
            if (!pi)
              continue;

            if (p.is_a<hxx> () || p.is_a<cxx> ())
            {
              // Start asynchronous matching of header and source file
              // prerequisites and store their targets.
              //
              const target& t (p.search (g));

              match_async (a, t, ctx.count_busy (), g[a].task_count);

              pts.emplace_back (&t, pi);
            }
            else
            {
              // Collect library prerequisites but fail in case of a lib{}
              // (see the compile rule for details).
              //
              if (p.is_a<libs>  ()  ||
                  p.is_a<liba>  ()  ||
                  p.is_a<libul> ()  ||
                  p.is_a<libux> ())
              {
                lib_prereqs.emplace_back (p.as_prerequisite ());
              }
              else if (p.is_a<bin::lib> ())
              {
                fail << "unable to extract preprocessor options for "
                     << g << " from " << p << " directly" <<
                  info << "instead go through a \"metadata\" utility library "
                       << "(either libul{} or libue{})" <<
                  info << "see qt.moc module documentation for details";
              }
            }
          }

          wg.wait ();

          // Finish matching, and then update, all the header and source file
          // prerequisite targets that we have started.
          //
          for (const prerequisite_target& pt: pts)
          {
            const target& t (*pt.target);

            match_direct_complete (a, t);

            // @@ TMP Presumably the fact that update_during_match(), which
            //        does execute_direct_sync(), is used during header
            //        extraction means it's OK here too?
            //
            if (a == perform_update_id)
              update_during_match (trace, a, t);
          }
        }

        // Discover group members (moc outputs) and match them asynchronously.
        //
        // @@ TODO Scan prerequisites for Qt meta-object macros and make
        //         members only for those that match.
        //
        //         For the time being we simply make members for all
        //         prerequisites.
        //
        g.reset_members (a);

        // Wait (for member match completion) with unlocked phase to allow
        // phase switching.
        //
        wait_guard wg (ctx, ctx.count_busy (), g[a].task_count, true);

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

          // Start asynchronous matching of the member.
          //
          match_async (a, m, ctx.count_busy (), g[a].task_count);

          g.members.push_back (&m);
        }

        wg.wait ();

        // Finish matching the member targets that we have started.
        //
        for (const cc* m: g.members)
          match_direct_complete (a, *m);

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

        // Note that perform is not executed normally, only when the group is
        // updated/cleaned directly.

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
        // execute_prerequisites (a, g); // @@ Not needed for update.

        target_state r (target_state::unchanged);

        // Execute members asynchronously.
        //
        // This is basically execute_members(), but based on
        // execute_direct_async() to complement our use of
        // match_async()/match_complete_direct() above.
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
