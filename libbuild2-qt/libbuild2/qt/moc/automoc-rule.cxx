#include <libbuild2/qt/moc/automoc-rule.hxx>

#include <libbuild2/depdb.hxx>
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

          // The depdb starts with the rule name and version followed by the
          // moc macro scan results of all header and source file
          // prerequisites, one per line, formatted as follows:
          //
          //   <macro-flag> <path>
          //
          // The flag is '1' if the file contains moc macros and '0' if not.
          // The scan results are terminated by a blank line.
          //
          // For example:
          //
          //   qt.moc.automoc 1
          //   1 /tmp/foo/hasmoc.h
          //   0 /tmp/foo/nomoc.h
          //
          //   ^@
          //

          // Match and update header and source file prerequisites and collect
          // library prerequisites.
          //
          // Note that we have to do this in the direct mode since we don't
          // know whether perform() will be executed or not.
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
                const target& pt (p.search (g));

                match_async (a, pt, ctx.count_busy (), g[a].task_count);

                pts.emplace_back (&pt, pi);
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

          // Create the output directory (for the depdb).
          //
          fsdir_rule::perform_update_direct (a, g);

          // Iterate over pts and depdb entries in parallel comparing each
          // pair of entries ("lookup mode"). If we encounter any kind of
          // deviation (no match, no entry on either side, mtime, etc), then
          // we switch depdb to writing and start scanning entries from pts
          // ("scan mode").
          //
          // Note that we have to store "negative" inputs (those that don't
          // contain any moc macros) in depdb since we cannot distinguish
          // between "negative" input that we have already scanned and a new
          // input that we haven't scanned.
          //

          // See above for a description of the depdb format.
          //
          depdb dd (g.dir / (g.name + ".automoc.d"));

          // If the rule name and/or version does not match we will be doing
          // an unconditional scan below.
          //
          if (dd.expect ("qt.moc.automoc 1") != nullptr)
            l4 ([&]{trace << "rule mismatch forcing update of " << g;});

          // Sort pts to ensure prerequisites line up with their depdb
          // entries. Skip the output directory if present.
          //
          // Note that it is certain at this point that everything in pts
          // except for dir are `path_target`s.
          //
          sort (pts.begin () + (dir == nullptr ? 0 : 1), pts.end (),
                [] (const prerequisite_target& x, const prerequisite_target& y)
                {
                  return x->as<path_target> ().path () <
                         y->as<path_target> ().path ();
                });

          for (const prerequisite_target& p: pts)
          {
            if (p == dir) // Skip the output directory injected above.
              continue;

            const path_target& pt (p->as<path_target> ());

            // If true, this prerequisite will be scanned and the result
            // written to the depdb.
            //
            bool scan (dd.writing ());

            // If we're still in lookup mode, read the next line from the
            // depdb and switch to scan mode if necessary; otherwise skip the
            // prerequisite if its depdb macro flag is false (i.e., don't add
            // it as a member).
            //
            if (!dd.writing ())
            {
              string* l (dd.read ());

              // Get the prerequisite's mtime.
              //
              timestamp mt;
              if ((mt = pt.mtime ()) == timestamp_unknown)
                mt = mtime (pt.path ().string ().c_str ());

              // Switch to scan mode if the prerequisite is newer than the
              // depdb or there's no valid depdb entry for it; otherwise check
              // the debdb entry.
              //
              if (mt > dd.mtime || l == nullptr || l->size () < 2)
                scan = true;
              else
              {
                // Switch to scan mode if the prerequisite path doesn't match
                // the depdb path; otherwise skip the prerequisite if its
                // depdb macro flag is false.
                //
                if (path (l->c_str () + 2) != pt.path ())
                  scan = true;
                else if (l->front () == '0')
                  continue;
              }
            }

            // Scan the prerequisite for moc macros if necessary and write the
            // result to the depdb. Skip the prerequisite if no macros were
            // found (i.e., don't add it as a member).
            //
            if (scan)
            {
              // @@ TODO: Implement scan.
              //

              dd.write ("1 ", false);
              dd.write (pt.path ());

              if (false) // No macros found.
                continue;
            }

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
            // correspond to the prerequisite, not the group (think of a
            // source in a subdirectory).
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

          // Write the blank line terminating the list of paths.
          //
          dd.expect ("");
          dd.close (false /* mtime_check */);

          // Match members asynchronously.
          //
          // Note that we have to also do this in the direct mode since we
          // don't know whether perform() will be executed or not.
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
          // @@ TMP Just do the bare minimum to get this action to work:
          //        basically we need a non-zero number of members.
          //
          // Match prerequisites.
          //
          match_prerequisite_members (a, g);

          // Discover group members.
          //
          if (g.group_members (a).members == nullptr)
          {
            g.reset_members (a);

            auto& pts (g.prerequisite_targets[a]);

            for (const prerequisite_target& p: pts)
            {
              if (p == dir) // Skip the output directory injected above.
                continue;

              const path_target& pt (p->as<path_target> ());
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
                continue;

              pair<target&, ulock> tl (
                search_new_locked (ctx,
                                   *mtt,
                                   pt.out_dir (), // dir
                                   dir_path (),   // out (always in out)
                                   move (mn),
                                   nullptr,       // ext
                                   nullptr));     // scope (absolute path)

              const cc& m (tl.first.as<cc> ()); // Member target.

              if (tl.second.owns_lock ())
                tl.second.unlock ();

              g.members.push_back (&m);
            }
          }

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
