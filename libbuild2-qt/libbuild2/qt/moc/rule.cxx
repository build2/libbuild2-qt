#include <libbuild2/qt/moc/rule.hxx>

#include <libbuild2/depdb.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>
#include <libbuild2/make-parser.hxx>

#include <libbuild2/c/target.hxx>
#include <libbuild2/cxx/target.hxx>

#include <libbuild2/qt/moc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace moc
    {
      struct compile_rule::match_data
      {
        match_data (const compile_rule& r, size_t pn) : pts_n (pn), rule (r) {}

        depdb::reopen_state dd;

        // The number of valid header paths read from the depdb.
        //
        size_t skip_count = 0;

        const size_t pts_n; // Number of static prerequisites.

        timestamp mt;

        const compile_rule& rule;

        target_state
        operator() (action a, const target& t)
        {
          return rule.perform_update (a, t, *this);
        }
      };

      // Return true if t has a prerequisite of type P with the specified
      // name, or any name if n is null.
      //
      template <typename P>
      static bool
      have_prereq (action a, const target& t, const char* n)
      {
        for (prerequisite_member p: group_prerequisite_members (a, t))
        {
          // If excluded or ad hoc, then don't factor it into our tests.
          //
          if (include (a, t, p) == include_type::normal)
            if (p.is_a<P> () && (n == nullptr || p.name () == n))
              return true;
        }

        return false;
      }

      bool
      compile_rule::match (action a,
                           target& t,
                           const string& hint,
                           match_extra&) const
      {
        tracer trace ("qt::moc::compile_rule::match");

        if (t.is_a<cxx::cxx> ())
        {
          // Enforce the moc naming conventions (cxx{moc_foo} from hxx{foo})
          // unless we have a rule hint, in which case we accept any filenames
          // and take the first hxx{} prerequisite as the input file.
          //
          const char* pn; // Prerequisite name.

          if (hint.empty ())
          {
            if (t.name.find ("moc_") == string::npos)
              return false;

            pn = t.name.c_str () + 4;
          }
          else
            pn = nullptr;

          if (have_prereq<cxx::hxx> (a, t, pn))
            return true;

          l4 ([&]{trace << "no header for target " << t;});
        }
        else if (t.is_a<qt::moc::moc> ())
        {
          if (have_prereq<cxx::cxx> (a, t, t.name.c_str ()))
            return true;

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
      apply (action a, target& xt) const
      {
        tracer trace ("qt::moc::compile_rule::apply");

        file& t (xt.as<file> ());
        t.derive_path ();
        const path& tp (t.path ());

        context& ctx (t.ctx);
        const scope& bs (t.base_scope ());

        // Inject dependency on the output directory.
        //
        inject_fsdir (a, t);

        // For update inject dependency on the MOC compiler target.
        //
        if (a == perform_update_id)
          inject (a, t, moc);

        // Match prerequisites.
        //
        match_prerequisite_members (a, t);

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

        // Get the first hxx{} or cxx{} prerequisite target.
        //
        const file* s;
        {
          for (prerequisite_target& p: t.prerequisite_targets[a])
          {
            if ((s = p->is_a<cxx::hxx> ()) || (s = p->is_a<cxx::cxx> ()))
              break;
          }

          assert (s != nullptr);
        }

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
          if (dd.expect (s->path ()) != nullptr)
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
        {
          auto& pts (t.prerequisite_targets[a]);

          for (prerequisite_target& p: pts)
            u = update (trace, a, *p.target, u ? timestamp_unknown : mt) || u;
        }

        match_data md (*this, t.prerequisite_targets[a].size ());

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
                  map_ext, c::h::static_type).first)
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

        context& ctx (t.ctx);
        const scope& bs (t.base_scope ());

        // Update prerequisites.
        //
        // Note that this is done purely to keep the dependency counts
        // straight. (All prerequisites were updated in apply() and thus their
        // states have already been factored into the update decision.)
        //
        {
          // Note: while strictly speaking we don't need the mtime check, this
          // is the most convenient version of execute_prerequisites().
          //
          optional<target_state> ps (execute_prerequisites (a, t, md.mt));

          if (ps)
            return *ps; // No need to update.

          assert (md.mt == timestamp_nonexistent);
        }

        // Get the hxx{} or cxx{} prerequisite target.
        //
        const file* s;
        {
          for (prerequisite_target& p: t.prerequisite_targets[a])
          {
            if (p.target != nullptr &&
                ((s = p->is_a<cxx::hxx> ()) || (s = p->is_a<cxx::cxx> ())))
              break;
          }

          assert (s != nullptr);
        }

        // Prepare the moc command line.
        //
        const process_path& pp (moc.process_path ());
        cstrings args {pp.recall_string ()};

        append_options (args, t, "qt.moc.options");

        // Translate output path to relative (to working directory) for easier
        // to read diagnostics. The input path, however, must be absolute
        // otherwise moc will put the relative path in the depfile.
        //
        path relo (relative (tp));            // Output path.
        path sn (s->path ().leaf ());         // Header/source file name.
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
        if (t.is_a<cxx::cxx> ())
        {
          args.push_back ("-f");
          args.push_back (sn.string ().c_str ());
        }
        else if (t.is_a<qt::moc::moc> ())
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
        args.push_back (s->path ().string ().c_str ());

        args.push_back (nullptr);

        if (verb >= 2)
          print_process (args);
        else if (verb)
          print_diag ("moc", *s, t);

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
                map_ext, c::h::static_type).first)
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
