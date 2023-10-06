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

        // Inject dependency on the output directory (for the depdb).
        //
        const target* dir (inject_fsdir (a, g));

        if (a == perform_update_id)
        {
          // The overall plan is as follows:
          //
          // 1. Match and update sources and headers (we need to update
          //    because we scan them), and collect all the library
          //    prerequisites (because we need to propagate them to
          //    prerequisites of dependencies that we synthesize).
          //
          // 2. Scan sources and headers for meta-object macros. Those that
          //    contain such macros are made members of this group and for
          //    them we synthesize a dependency and match the moc compile
          //    rule.

          // Create the output directory (for the depdb).
          //
          fsdir_rule::perform_update_direct (a, g);

          // Match and update header and source file prerequisites and collect
          // library prerequisites.
          //
          vector<prerequisite> libs;

          auto& pts (g.prerequisite_targets[a]);
          {
            // Wait with unlocked phase to allow phase switching.
            //
            wait_guard wg (ctx, ctx.count_busy (), g[a].task_count, true);

            for (const prerequisite_member& p: group_prerequisite_members (a, g))
            {
              include_type pi (include (a, g, p));

              // Ignore excluded.
              //
              if (!pi)
                continue;

              // Fail if there are any ad hoc prerequisites because perform is
              // not normally executed.
              //
              if (pi == include_type::adhoc)
                fail << "ad hoc prerequisite " << p << " of target " << g
                     << " does not make sense";

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
                // (see the compile rule we are delegating to for details).
                //
                if (p.is_a<bin::libs>  ()  ||
                    p.is_a<bin::liba>  ()  ||
                    p.is_a<bin::libul> ()  ||
                    p.is_a<bin::libux> ())
                {
                  libs.emplace_back (p.as_prerequisite ());
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

            // Finish matching all the header and source file prerequisite
            // targets that we have started.
            //
            for (const prerequisite_target& pt: pts)
              match_direct_complete (a, *pt);
          }

          // Update the header and source file prerequisites.
          //
          update_during_match_prerequisites (trace, a, g, 0);

          // Discover group members (moc outputs).
          //
          // @@ TODO Scan prerequisites for Qt meta-object macros and make
          //         members only for those that match.
          //
          //         For the time being we simply make members for all
          //         prerequisites.
          //
          g.reset_members (a);

          for (const prerequisite_target& p: pts)
          {
            const target& pt (*p);

            if (&pt == dir) // Skip output directory injected above.
              continue;

            // Derive the moc output name and target type.
            //
            string mn;              // Member target name.
            const target_type* mtt; // Member target type.

            if (pt.is_a<hxx> ())
            {
              mn = "moc_" + pt.name;
              mtt = &cxx::static_type;
            }
            else if (pt.is_a<cxx> ())
            {
              mn = pt.name;
              mtt = &moc::static_type;
            }
            else // Not a header or source file?
            {
              assert (false);
              continue;
            }

            // Prepare member's prerequisites: the header or source file and
            // all of the library prerequisites.
            //
            prerequisites ps {prerequisite (pt)};
            for (const prerequisite& l: libs)
              ps.push_back (l);

            // Search for an existing target or create a new one.
            //
            // Note: we want to use the prerequisite's output directory rather
            // than the group's since we want the member's location to
            // correspond to the prerequisite, not the group.
            //
            pair<target&, ulock> tl (
              search_new_locked (ctx,
                                 *mtt,
                                 pt.out_dir (), // dir
                                 dir_path (),   // out (always in out)
                                 move (mn),
                                 nullptr,       // ext
                                 nullptr));     // scope (absolute path)

            const cc& m (tl.first.as<cc> ()); // Member target.

            // We are ok with an existing target as long as it doesn't have any
            // prerequisites. For example, the user could have specified a
            // target-specific variable.
            //
            // Note also that we may have already done this before in case of
            // an operation batch.
            //
            if (!m.prerequisites (move (ps))) // Note: cannot fail if locked.
            {
              // @@ TODO: verify prerequisites match.
            }

            if (tl.second.owns_lock ())
              tl.second.unlock ();

            g.members.push_back (&m);
          }

          // Match members asynchronously.
          //
          // Note that we have to do this in the direct mode since we don't
          // know whether perform() will be executed or not.
          //
          // Wait with unlocked phase to allow phase switching.
          //
          wait_guard wg (ctx, ctx.count_busy (), g[a].task_count, true);

          for (const cc* m: g.members)
            match_async (a, *m, ctx.count_busy (), g[a].task_count);

          wg.wait ();

          for (const cc* m: g.members)
            match_direct_complete (a, *m);
        }
        else if (a == perform_clean_id)
        {
          fail << "clean not supported yet";
          return noop_recipe;
        }
        else // Configure/dist update.
        {
          return noop_recipe;
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

        // Note that perform is not executed normally, only when the group is
        // updated/cleaned directly.
        //
        // Note that all the prerequisites have been matched and updated in
        // the direct mode which means no dependency counts to keep straight
        // and thus no need to execute them here.

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
