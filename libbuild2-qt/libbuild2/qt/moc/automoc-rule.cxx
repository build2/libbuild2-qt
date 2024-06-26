#include <libbuild2/qt/moc/automoc-rule.hxx>

#include <libbuild2/depdb.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>

#include <libbuild2/bin/target.hxx>

#include <libbuild2/cc/lexer.hxx>

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

        if (a != perform_update_id &&
            a != perform_clean_id) // Configure/dist update.
        {
          // Leave members empty if they haven't been discovered yet.
          //
          if (g.group_members (a).members == nullptr)
            g.reset_members (a);

          return noop_recipe;
        }

        path dd_path (g.dir / (g.name + ".automoc.d")); // Depdb path.

        auto& pts (g.prerequisite_targets[a]);

        // Inject dependency on the output directory (for the depdb).
        //
        const fsdir* dir (inject_fsdir_direct (a, g));
        if (dir != nullptr)
        {
          // Since we don't need to propagate fsdir{} to perform() (which may
          // not be called; see below), pop it out of prerequisite_targets to
          // simplify things.
          //
          assert (pts.back () == dir);
          pts.pop_back ();
        }

        // Extra prerequisites to be propagated to the moc rule: libraries and
        // ad hoc headers.
        //
        vector<prerequisite> extras;

        auto inject_member = [&ctx, &g, &extras] (const path_target& pt)
        {
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
            return;
          }

          // Prepare member's prerequisites: the input header or source file
          // and all of the ad hoc headers and library prerequisites.
          //
          prerequisites ps {prerequisite (pt)};
          for (const prerequisite& p: extras)
            ps.push_back (p);

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
                               pt.out_dir (),     // dir
                               dir_path (),       // out (always in out)
                               move (mn),
                               nullptr,           // ext
                               nullptr));         // scope (absolute path)

          const cc& m (tl.first.as<cc> ()); // Member target.

          // We are ok with an existing target if it has no prerequisites (for
          // example, the user could have specified a target-specific
          // variable) or compatible prerequisites (see below).
          //
          // Note also that we may have already done this before in case of an
          // operation batch.
          //
          if (!m.prerequisites (move (ps))) // Note: cannot fail if locked.
          {
            // For now we just verify that the first prerequisite is our
            // header/source file. In particular, this leaves the door open
            // for the user to specify a custom dependency declaration and we
            // are ok with that as long as it still looks like moc.
            //
            const prerequisites& eps (m.prerequisites ());

            // Member's existing header/source file prerequisite and target.
            //
            const prerequisite* ep (eps.empty () ? nullptr : &eps.front ());
            const target* et (ep != nullptr ? &search (m, *ep) : nullptr);

            if (et != &pt)
            {
              diag_record dr (fail);

              dr << "synthesized dependency for prerequisite " << ps.front ()
                 << " would be incompatible with existing target " << m;

              if (et == nullptr)
                dr << info << "no existing header/source prerequisite";
              else
              {
                dr << info << "existing header/source prerequisite "
                   << *ep << " does not match " << ps.front ();
              }
            }
          }

          if (tl.second.owns_lock ())
          {
            // Link up member to group. This will be the common case where we
            // will be creating the member. See match_members below for the
            // uncommon case where the member is declared in the buildfile.
            //
            tl.first.group = &g;

            tl.second.unlock ();
          }

          g.members.push_back (&m);
        };

        // Match members asynchronously.
        //
        // Note that we have to also do this in the direct mode since we don't
        // know whether perform() will be executed or not.
        //
        auto match_members = [&ctx, a, &g] ()
        {
          // Wait with unlocked phase to allow phase switching.
          //
          wait_guard wg (ctx, ctx.count_busy (), g[a].task_count, true);

          for (const cc* pm: g.members)
          {
            const cc& m (*pm);

            // Link up member to group (unless already done; see inject_member
            // above).
            //
            if (m.group != &g) // Note: atomic.
            {
              // We can only update the group under lock.
              //
              target_lock tl (lock (a, m));

              if (!tl)
                fail << "group " << g << " member " << m
                     << " is already matched" <<
                  info << "automoc{} group members cannot be used as "
                       << "prerequisites directly, only via group";

              if (m.group == nullptr)
                tl.target->group = &g;
              else if (m.group != &g)
              {
                fail << "group " << g << " member " << m
                     << " is already member of group " << *m.group;
              }
            }

            match_async (a, m, ctx.count_busy (), g[a].task_count);
          }

          wg.wait ();

          for (const cc* pm: g.members)
            match_direct_complete (a, *pm);
        };

        if (a == perform_update_id)
        {
          // The overall plan is as follows:
          //
          // 1. Match and update the input sources and headers (we need to
          //    update because we scan them), and collect ad hoc header and
          //    library prerequisites (because we need to propagate them to
          //    prerequisites of dependencies that we synthesize).
          //
          // 2. Scan the input sources and headers for meta-object macros
          //    ("moc macros"). For each of those that contain such macros we
          //    synthesize a moc output target and dependency, make the target
          //    a member, and match the moc compile rule.

          // Match and update the input header and source file prerequisites
          // and collect ad hoc headers and library prerequisites.
          //
          // Note that we have to do this in the direct mode since we don't
          // know whether perform() will be executed or not.
          //
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

              // Collect ad hoc headers or fail if there are any other types
              // of ad hoc prerequisites because perform is not normally
              // executed.
              //
              if (pi == include_type::adhoc)
              {
                if (p.is_a<h> () || p.is_a<hxx> ())
                  extras.push_back (p.as_prerequisite ());
                else
                  fail << "ad hoc prerequisite " << p << " of target " << g
                       << " does not make sense";
              }
              else if (p.is_a<hxx> () || p.is_a<cxx> ())
              {
                // Start asynchronous matching of input header and source file
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
                  extras.emplace_back (p.as_prerequisite ());
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

            // Finish matching all the input header and source file
            // prerequisite targets that we have started.
            //
            for (const prerequisite_target& pt: pts)
              match_direct_complete (a, *pt);
          }

          // Update the input header and source file prerequisites.
          //
          update_during_match_prerequisites (trace, a, g, 0);

          // Discover group members (moc outputs).
          //
          g.reset_members (a);

          // Create the output directory (for the depdb).
          //
          if (dir != nullptr)
            fsdir_rule::perform_update_direct (a, *dir);

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
          // The depdb starts with the rule name and version followed by the
          // moc macro scan results of all input header and source file
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
          depdb dd (dd_path);

          // If the rule name and/or version does not match we will be doing
          // an unconditional scan below.
          //
          if (dd.expect (rule_id_) != nullptr)
            l4 ([&]{trace << "rule mismatch forcing rescan of " << g;});

          // Sort pts to ensure prerequisites line up with their depdb
          // entries.
          //
          // Note that it is certain at this point that everything in pts are
          // path_target's.
          //
          sort (pts.begin (), pts.end (),
                [] (const prerequisite_target& x, const prerequisite_target& y)
                {
                  // Note: we have observed the match of all these targets so
                  // we can use the relaxed memory order for path().
                  //
                  return x->as<path_target> ().path (memory_order_relaxed) <
                         y->as<path_target> ().path (memory_order_relaxed);
                });

          for (const prerequisite_target& p: pts)
          {
            const path_target& pt (p->as<path_target> ());
            const path& ptp (pt.path (memory_order_relaxed)); // See above.

            // True if this prerequisite needs to be scanned and the result
            // written to the depdb.
            //
            bool scan;

            if (dd.writing ())
              scan = true;
            else
            {
              // If we're still in the lookup mode, read the next line from
              // the depdb and switch to scan mode if necessary; otherwise
              // skip the prerequisite if its depdb macro flag is false (i.e.,
              // don't add its moc output as member).
              //
              string* l (dd.read ());

              // Switch to scan mode if the depdb entry is invalid or a blank
              // line or its path doesn't match the prerequisite's
              // path. Otherwise check its mtime and depdb macro flag.
              //
              if (l == nullptr || l->size () < 3 ||
                  path_traits::compare (l->c_str () + 2,
                                        l->size () - 2,
                                        ptp.string ().c_str (),
                                        ptp.string ().size ()) != 0)
              {
                scan = true;
              }
              else
              {
                // Get the prerequisite's mtime.
                //
                timestamp mt (pt.load_mtime ());

                // Switch to the scan mode if the prerequisite is newer than
                // the depdb; otherwise skip the prerequisite if its depdb
                // macro flag is false.
                //
                if (mt > dd.mtime)
                  scan = true;
                else
                {
                  if (l->front () == '0')
                    continue;
                  else
                    scan = false;
                }
              }
            }

            // Scan the prerequisite for moc macros if necessary and write the
            // result to the depdb. Skip the prerequisite if no macros were
            // found (i.e., don't add its moc output as member).
            //
            // @@ TODO: one day, maybe we could do this in parallel?
            //
            if (scan)
            {
              using namespace build2::cc; // lexer, token, token_type

              ifdstream is (ifdstream::badbit);
              try
              {
                is.open (ptp);
              }
              catch (const io_error& e)
              {
                fail << "unable to open file " << ptp << ": " << e;
              }

              bool macro (false); // True if a moc macro was found.

              path_name pn (ptp);
              lexer l (is, pn, false /* preprocessed */);

              for (token t (l.next ());
                   t.type != token_type::eos;
                   t = l.next ())
              {
                if (t.type == token_type::identifier)
                {
                  if (t.value == "Q_OBJECT"    ||
                      t.value == "Q_GADGET"    ||
                      t.value == "Q_NAMESPACE" ||
                      t.value == "Q_NAMESPACE_EXPORT")
                  {
                    macro = true;
                    break;
                  }
                }
              }

              dd.write (macro ? "1 " : "0 ", false);
              dd.write (ptp);

              if (!macro)
                continue;
            }

            // This prerequisite contains moc macros so synthesize its moc
            // output target and dependency and add the target as member.
            //
            inject_member (pt);
          }

          // Write the blank line terminating the list of paths.
          //
          dd.expect ("");
          dd.close (false /* mtime_check */);

          match_members ();
        }
        else // perform_clean_id
        {
          // It's a bit fuzzy whether we should also clean the input header
          // and source prerequisites which we've updated in update. The
          // representative corner case here would be a generated header that
          // doesn't actually contain any moc macros. But it's unclear doing
          // direct clean is a good idea due to execution order.
          //
          // - We could probably assume/expect that these headers/sources
          //   are also listed as prerequisites of other targets and will
          //   therefore be cleaned via that path.
          //
          // - But there is still the case where the group is cleaned
          //   directly. Perhaps in this case we should just do it in
          //   perform. But we don't know whether perform will be called
          //   or not.
          //
          // So feels like we don't have much choice except do the same as in
          // update.
          //
          // @@ But we could at least try to do unmatch, if possible. But for
          //    that to work we would ideally want to match members first
          //    (since they could be the reason we would be able to unmatch).
          //    Maybe later.
          //

          // Match and clean (later) input header and source file
          // prerequisites and collect ad hoc headers and library
          // prerequisites (because we need to propagate them to prerequisites
          // of dependencies that we synthesize).
          //
          // Note that we have to do this in the direct mode since we don't
          // know whether perform() will be executed or not.
          //
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

              // Collect ad hoc headers or fail if there are any other types
              // of ad hoc prerequisites because perform is not normally
              // executed.
              //
              if (pi == include_type::adhoc)
              {
                if (p.is_a<h> () || p.is_a<hxx> ())
                  extras.push_back (p.as_prerequisite ());
                else
                  fail << "ad hoc prerequisite " << p << " of target " << g
                       << " does not make sense";
              }
              else if (p.is_a<hxx> () || p.is_a<cxx> ())
              {
                // Start asynchronous matching of input header and source file
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
                  extras.emplace_back (p.as_prerequisite ());
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

            // Finish matching all the input header and source file
            // prerequisite targets that we have started.
            //
            for (const prerequisite_target& pt: pts)
              match_direct_complete (a, *pt);
          }

          // Sort prerequisites so that they can be binary-searched.
          //
          // Note that it is certain at this point that everything in pts are
          // path_target's.
          //
          sort (pts.begin (), pts.end (),
                [] (const prerequisite_target& x, const prerequisite_target& y)
                {
                  return x->as<path_target> ().path (memory_order_relaxed) <
                         y->as<path_target> ().path (memory_order_relaxed);
                });

          // The plan here is to recreate the members based on the information
          // saved in depdb.
          //
          g.reset_members (a);

          depdb dd (dd_path, true /* read_only */);

          while (dd.reading ()) // Breakout loop.
          {
            string* l;
            auto read = [&dd, &l] () -> bool
            {
              return (l = dd.read ()) != nullptr;
            };

            if (!read ()) // Rule id.
              break;

            if (*l != rule_id_)
              fail << "unable to clean target " << g
                   << " with old dependency database";

            // Read the line corresponding to the header and source
            // prerequisites. We should always end with a blank line.
            //
            for (;;)
            {
              if (!read ())
                break;

              if (l->empty () || l->size () < 3)
                break; // Done or invalid line.

              // Compare a prerequisite_target's path to a C string path from
              // the depdb.
              //
              struct cmp
              {
                bool
                operator() (const prerequisite_target& pt,
                            const pair<const char*, size_t>& ddp) const
                {
                  const string& ptp (
                    pt->as<path_target> ().path (memory_order_relaxed).string ());

                  return path_traits::compare (ptp.c_str (), ptp.size (),
                                               ddp.first,    ddp.second) < 0;
                }

                bool
                operator() (const pair<const char*, size_t>& ddp,
                            const prerequisite_target& pt) const
                {
                  const string& ptp (
                    pt->as<path_target> ().path (memory_order_relaxed).string ());

                  return path_traits::compare (ddp.first,    ddp.second,
                                               ptp.c_str (), ptp.size ()) < 0;
                }
              };

              // Search for the path (at l[2]) in pts.
              //
              auto pr (equal_range (pts.begin (), pts.end (),
                                    make_pair (l->c_str () + 2, l->size () - 2),
                                    cmp ()));

              // Skip if there is no prerequisite with this path.
              //
              if (pr.first == pr.second)
                continue;

              prerequisite_target& pt (*pr.first);

              // Inject a member for this prerequisite if it contains moc
              // macros.
              //
              if (l->front () == '1')
                inject_member (pt->as<path_target> ());
            }

            break;
          }

          match_members ();

          // Clean the input header and source file prerequisites.
          //
          clean_during_match_prerequisites (trace, a, g, 0);

          // We also need to clean the depdb file here (since perform may not
          // get executed). Let's also factor the match-only mode here since
          // once we remove the file, we won't be able to clean the members.
          //
          if (!ctx.match_only)
            rmfile (ctx, dd_path, 2 /* verbosity */);

          // Remove the output directory (if we can).
          //
          if (dir != nullptr)
           fsdir_rule::perform_clean_direct (a, *dir);
        }

        return &perform;
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
