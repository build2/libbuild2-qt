#include <libbuild2/qt/moc/rule.hxx>

#include <libbuild2/depdb.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>
#include <libbuild2/make-parser.hxx>

#include <libbuild2/bin/target.hxx>

#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      struct compile_rule::match_data
      {
        match_data (const compile_rule& r, const file& s, size_t pn)
            : src (s), pts_n (pn), rule (r)
        {
        }

        depdb::reopen_state dd;

        // The number of valid header paths read from the depdb.
        //
        size_t skip_count = 0;

        const file& src; // The source prerequisite target.

        const size_t pts_n; // Number of static prerequisites.

        timestamp mt;

        const compile_rule& rule;

        target_state
        operator() (action a, const target& t)
        {
          return rule.perform_update (a, t, *this);
        }
      };

      bool compile_rule::
      match (action a,
             target& t,
             const string& hint,
             match_extra& me) const
      {
        tracer trace ("qt::moc::compile_rule::match");

        // Find and return a prerequisite of type tt with the specified name,
        // or any name if n is null. Return nullopt if there is no such
        // prerequisite.
        //
        auto find_prereq = [a, &t] (const target_type& tt, const char* n)
          -> optional<prerequisite_member>
        {
          for (prerequisite_member p: group_prerequisite_members (a, t))
          {
            // If excluded or ad hoc, then don't factor it into our tests.
            //
            if (include (a, t, p) == include_type::normal)
              if (p.is_a (tt) && (n == nullptr || p.name () == n))
                return p;
          }

          return nullopt;
        };

        // Check whether we have a suitable target and source prerequisite.
        //
        // Note that the source prerequisite is passed to apply() via
        // match_extra and from there to perform_update() via match_data.
        //
        if (t.is_a<cxx> ())
        {
          // Enforce the moc naming conventions (cxx{moc_foo} from hxx{foo})
          // unless we have a rule hint, in which case we accept any filename
          // and take the first hxx{} prerequisite as the input file.
          //
          const char* pn; // Prerequisite name.

          if (hint.empty ())
          {
            if (t.name.compare (0, 4, "moc_") != 0)
            {
              l4 ([&]{trace << "no moc_ prefix in target " << t;});
              return false;
            }

            pn = t.name.c_str () + 4;
          }
          else
            pn = nullptr;

          if (auto p = find_prereq (hxx::static_type, pn))
          {
            me.data (*p); // Pass prerequisite to apply().
            return true;
          }

          l4 ([&]{trace << "no header file for target " << t;});
        }
        else if (t.is_a<moc> ())
        {
          if (auto p = find_prereq (cxx::static_type, t.name.c_str ()))
          {
            me.data (*p); // Pass prerequisite to apply().
            return true;
          }

          l4 ([&]{trace << "no source file for target " << t;});
        }

        return false;
      }

      // @@ TODO Handle plugin metadata json files specified via
      //         Q_PLUGIN_METADATA macros. (This is the only other file type
      //         supported besides headers and source files.)
      //
      static small_vector<const target_type*, 2>
      map_ext (const scope& bs, const string& n, const string& e)
      {
        return dyndep_rule::map_extension (bs, n, e, nullptr);
      }

      recipe compile_rule::
      apply (action a, target& xt, match_extra& me) const
      {
        tracer trace ("qt::moc::compile_rule::apply");

        // The prerequisite_target::include bits that indicate an unmatched
        // library.
        //
        const uintptr_t include_unmatch = 0x100;

        file& t (xt.as<file> ());
        const path& tp (t.derive_path ());

        context& ctx (t.ctx);
        const scope& bs (t.base_scope ());

        // Inject dependency on the output directory.
        //
        const target* dir (inject_fsdir (a, t));

        // For update inject dependency on the MOC compiler target.
        //
        if (a == perform_update_id)
          inject (a, t, ctgt);

        // Return true if a target type is a library.
        //
        auto is_lib = [] (const target_type& tt)
        {
          using namespace bin;

          return tt.is_a (libx::static_type) || tt.is_a (liba::static_type) ||
                 tt.is_a (libs::static_type) || tt.is_a (libux::static_type);
        };

        // Match static prerequisites.
        //
        // This is essentially match_prerequisite_members() but with support
        // for unmatching library prerequisites.
        //
        // Unmatched libraries are not updated at all; libraries that cannot
        // be unmatched are updated during execute only; all other types of
        // prerequisites are updated both here, during match, and during
        // execute.
        //
        // @@ TODO Explain the function of static library prerequisites
        //         somewhere.
        //
        auto& pts (t.prerequisite_targets[a]);
        {
          // Start asynchronous matching of prerequisites. Wait with unlocked
          // phase to allow phase switching.
          //
          wait_guard wg (ctx, ctx.count_busy (), t[a].task_count, true);

          for (prerequisite_member p: group_prerequisite_members (a, t))
          {
            const target* pt (nullptr);
            include_type  pi (include (a, t, p));

            // Ignore excluded.
            //
            if (!pi)
              continue;

            if (pi == include_type::normal && is_lib (p.type ()))
            {
              if (a.operation () != update_id)
                continue;

              // Handle (phase two) imported libraries.
              //
              // if (p.proj ())
              // {
              //   pt = search_library (a,
              //                        sys_lib_dirs,
              //                        usr_lib_dirs,
              //                        p.prerequisite);
              // }

              if (pt == nullptr)
                pt = &p.search (t);
            }
            else
            {
              pt = &p.search (t);

              // Don't add injected fsdir{} or compiler target twice.
              //
              if (pt == dir || pt == &ctgt)
                continue;

              if (a.operation () == clean_id && !pt->in (*bs.root_scope ()))
                continue;
            }

            match_async (a, *pt, ctx.count_busy (), t[a].task_count);

            pts.emplace_back (pt, pi);
          }

          wg.wait ();

          // Finish matching all the targets that we have started.
          //
          for (prerequisite_target& pt: pts)
          {
            if (pt == dir || pt == &ctgt) // See above.
              continue;

            if (is_lib (pt->type ()))
            {
              // @@ TMP This actually always calls match_impl first and then
              //        does the unmatch stuff afterwards.
              //
              pair<bool, target_state> mr (match_complete (a,
                                                           *pt.target,
                                                           unmatch::safe));

              l6 ([&]{trace << "unmatch " << *pt.target << ": " << mr.first;});

              if (mr.first)
              {
                pt.include |= include_unmatch;

                // Move the target pointer to data to prevent the prerequisite
                // from being updated while keeping its target around so that
                // its options can be extracted later.
                //
                pt.data = reinterpret_cast<uintptr_t> (pt.target);
                pt.target = nullptr;
                pt.include |= prerequisite_target::include_target;
              }
            }
            else
              match_complete (a, *pt.target);
          }
        }

        if (a == perform_clean_id)
        {
          return [] (action a, const target& t)
          {
            return perform_clean_extra (a, t.as<file> (), {".d", ".t"});
          };
        }
        else if (a != perform_update_id)
          return noop_recipe; // Configure/dist update.

        // This is a perform update.

        // Retrieve the source prerequisite target from match_extra.
        //
        // Note that this prerequisite should have been searched by the
        // match_prerequisite_members() call above and therefore we can just
        // load it. @@ Is there a cleaner way to do this?
        //
        const file& s (me.data<prerequisite_member> ().load ()->as<file> ());

        // Create the output directory.
        //
        fsdir_rule::perform_update_direct (a, t);

        // We use depdb to track changes to the input file name, options,
        // compiler, etc.
        //
        depdb dd (tp + ".d");
        {
          // First should come the rule name/version.
          //
          if (dd.expect ("qt.moc.compile 1") != nullptr)
            l4 ([&]{trace << "rule mismatch forcing update of " << t;});

          // Then the compiler checksum.
          //
          if (dd.expect (csum) != nullptr)
            l4 ([&]{trace << "compiler mismatch forcing update of " << t;});

          // Then the options checksum.
          //
          {
            sha256 cs;
            append_options (cs, t, "qt.moc.options");

            if (dd.expect (cs.string ()) != nullptr)
              l4 ([&]{trace << "options mismatch forcing update of " << t;});
          }

          // Finally the input file.
          //
          if (dd.expect (s.path ()) != nullptr)
            l4 ([&]{trace << "input file mismatch forcing update of " << t;});
        }

        // Determine if we need to do an update based on the above checks.
        //
        bool u; // True if the target needs to be updated.
        timestamp mt;

        if (dd.writing ())
        {
          u = true;
          mt = timestamp_nonexistent;
        }
        else
        {
          if ((mt = t.mtime ()) == timestamp_unknown)
            t.mtime (mt = mtime (tp));

          u = dd.mtime > mt;
        }

        // Update the static prerequisites.
        //
        for (prerequisite_target& p: pts)
        {
          // Skip library prerequisites, both unmatched (never updated) and
          // matched (updated only during execute).
          //
          // @@ TODO Detect changed export.poptions and set u=true if so.
          //
          if (((p.include & include_unmatch) != 0) || is_lib (p->type ()))
            continue;

          u = update (trace, a, *p.target, u ? timestamp_unknown : mt) || u;
        }

        match_data md (*this, s, t.prerequisite_targets[a].size ());

        // Verify the header paths in the depdb unless we're already updating
        // (in which case they will be overwritten in perform_update()).
        //
        if (!u)
        {
          // Find or enter a header as a target, update it, and inject it as a
          // prerequisite target.
          //
          // Return true if the header has changed and nullopt if it does not
          // exist.
          //
          auto add = [&trace, a, &bs, &t, mt] (path fp) -> optional<bool>
          {
            // If it is outside any project, or the project doesn't have such
            // an extension, assume it is a plain old C header.
            //
            if (const build2::file* ft = enter_file (
                  trace, "header",
                  a, bs, t,
                  fp, true /* cache */, true /* normalized */,
                  map_ext, h::static_type).first)
            {
              // Note that static prerequisites are never written to the
              // depdb.
              //
              if (optional<bool> u = inject_existing_file (
                    trace, "header",
                    a, t, 0 /* pts_n */,
                    *ft, mt,
                    false /* fail */))
              {
                return *u;
              }
            }

            return nullopt;
          };

          auto df = make_diag_frame (
            [&t] (const diag_record& dr)
            {
              if (verb != 0)
                dr << info << "while extracting dynamic dependencies for " << t;
            });

          // Read the header paths from the depdb.
          //
          while (!u)
          {
            // We should always end with a blank line.
            //
            string* l (dd.read ());

            // If the line is invalid, run moc.
            //
            if (l == nullptr)
            {
              u = true;
              break;
            }

            if (l->empty ()) // Done, nothing changed.
              break;

            if (optional<bool> r = add (path (*l)))
            {
              // Count valid header path lines so that, if we encounter an
              // invalid one, we know how many to skip when updating the depdb
              // from moc's depfile later.
              //
              md.skip_count++;

              if (*r)
                u = true;
            }
            else
            {
              // Header does not exist. Invalidate this line and trigger
              // update.
              //
              dd.write ();
              u = true;
            }
          }
        }

        // Note that during a dry run we may end up with an incomplete (but
        // valid) database, but it will be updated on the next non-dry run.
        //
        if (!u || ctx.dry_run_option)
          dd.close (false /* mtime_check */);
        else
          md.dd = dd.close_to_reopen ();

        // Pass on update/mtime.
        //
        md.mt = u ? timestamp_nonexistent : mt;

        return md;
      }

      target_state compile_rule::
      perform_update (action a, const target& xt, match_data& md) const
      {
        tracer trace ("qt::moc::compile_rule::perform_update");

        const file& t (xt.as<file> ());
        const path& tp (t.path ());

        const file& s (md.src);
        const path& sp (s.path ());

        context& ctx (t.ctx);
        const scope& bs (t.base_scope ());

        // Update prerequisites.
        //
        // Note that, with the exception of matched libraries which are being
        // updated here for the first time, this is done purely to keep the
        // dependency counts straight. (All non-library prerequisites were
        // updated in apply() and thus their states have already been factored
        // into the update decision.)
        //
        {
          // Note that the mtime check is only necessary for the matched
          // library prerequisites (and unmatched libraries will be skipped).
          //
          // @@ We actually want to ignore any changes to libraries that
          //    got updated (since they don't affect the result).
          //
          optional<target_state> ps (execute_prerequisites (a, t, md.mt));

          if (ps)
            return *ps; // No need to update.

          // @@ TODO This will fail if a library prereq is out of date at this
          //         point. @@ Should be fixed by above fix.
          //
          assert (md.mt == timestamp_nonexistent);
        }

        // Prepare the moc command line.
        //
        const process_path& pp (ctgt.process_path ());
        cstrings args {pp.recall_string ()};

        append_options (args, t, "qt.moc.options");

        // Translate output path to relative (to working directory) for easier
        // to read diagnostics. The input path, however, must be absolute
        // otherwise moc will put the relative path in the depfile.
        //
        path relo (relative (tp));            // Output path.
        path sn (sp.leaf ());                 // Source file name.
        path depfile (relo.string () + ".t"); // Depfile path.

        // If we're generating a cxx{}, pass -f to override the path with
        // which the input header will be #include'd, which is relative to the
        // output directory, with just the name of the input file.
        //
        // Otherwise, if we're generating a moc{} -- which is included -- pass
        // -i to prevent any #include directive from being generated for the
        // input source file (otherwise we'd get multiple definitions errors
        // if the input source file is also compiled, as is typical).
        //
        // @@ Need to support/expose include prefix (<moc/...>). Let's
        //    do for starters via `qt.moc.options = -p moc/`.
        //
        if (t.is_a<cxx> ())
        {
          args.push_back ("-f");
          args.push_back (sn.string ().c_str ());
        }
        else if (t.is_a<moc> ())
          args.push_back ("-i");

        // Depfile path.
        //
        args.push_back ("--output-dep-file");
        args.push_back ("--dep-file-path");
        args.push_back (depfile.string ().c_str ());

        // Output path.
        //
        args.push_back ("-o");
        args.push_back (relo.string ().c_str ());

        // Input path.
        //
        args.push_back (sp.string ().c_str ());

        args.push_back (nullptr);

        if (verb >= 2)
          print_process (args);
        else if (verb)
          print_diag ("moc", s, t);

        // Sequence start time for mtime checks below.
        //
        timestamp start (!ctx.dry_run && depdb::mtime_check ()
                         ? system_clock::now ()
                         : timestamp_unknown);

        if (!ctx.dry_run)
          run (ctx, pp, args, 1 /* finish_verbosity */);

        // Write the header paths contained in the moc-generated depfile to
        // the depdb.
        //
        if (!ctx.dry_run)
        {
          depdb dd (move (md.dd));
          size_t skip (md.skip_count);

          // Note that fp is expected to be absolute.
          //
          auto add = [&trace,
                      a, &bs, &t, pts_n = md.pts_n,
                      &dd, &skip] (path fp)
          {
            normalize_external (fp, "header");

            // If it is outside any project, or the project doesn't have such
            // an extension, assume it is a plain old C header.
            //
            if (const build2::file* ft = find_file (
                trace, "header",
                a, bs, t,
                fp, false /* cache */, true /* normalized */,
                true /* dynamic */,
                map_ext, h::static_type).first)
            {
              // Do not store static prerequisites in the depdb.
              //
              {
                auto& pts (t.prerequisite_targets[a]);

                for (size_t i (0); i != pts_n; ++i)
                  if (pts[i].target == ft)
                    return;
              }

              // Skip those header paths that already exist in the depdb.
              //
              if (skip != 0)
              {
                --skip;
                return;
              }

              // Verify it has noop recipe.
              //
              verify_existing_file (trace, "header", a, t, pts_n, *ft);
            }

            dd.write (fp);
          };

          auto df = make_diag_frame (
            [&t] (const diag_record& dr)
            {
              if (verb != 0)
                dr << info << "while extracting dynamic dependencies for "
                           << t;
            });

          // Open and parse the depfile (in make format).
          //
          ifdstream is (ifdstream::badbit);
          try
          {
            is.open (depfile);
          }
          catch (const io_error& e)
          {
            fail << "unable to open file " << depfile << ": " << e;
          }

          location il (depfile, 1);

          using make_state = make_parser;
          using make_type = make_parser::type;

          make_parser make;

          for (string l;; ++il.line) // Reuse the buffer.
          {
            if (eof (getline (is, l)))
            {
              if (make.state != make_state::end)
                fail (il) << "incomplete make dependency declaration";

              break;
            }

            size_t pos (0);
            do
            {
              // Note that we don't really need a diag frame that prints the
              // line being parsed since we are always parsing the file.
              //
              pair<make_type, path> r (make.next (l, pos, il));

              if (r.second.empty ())
                continue;

              if (r.first == make_type::target)
                continue;

              add (move (r.second));
            }
            while (pos != l.size ());

            if (make.state == make_state::end)
              break;
          }

          // Add the terminating blank line.
          //
          dd.expect ("");
          dd.close ();

          md.dd.path = move (dd.path); // For mtime check below.
        }

        timestamp now (system_clock::now ());

        if (!ctx.dry_run)
          depdb::check_mtime (start, md.dd.path, t.path (), now);

        t.mtime (now);

        return target_state::changed;
      }
    }
  }
}
