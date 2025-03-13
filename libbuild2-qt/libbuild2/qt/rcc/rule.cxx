#include <libbuild2/qt/rcc/rule.hxx>

#include <libbuild2/depdb.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>
#include <libbuild2/make-parser.hxx>

#include <libbuild2/qt/rcc/target.hxx>

// @@ TODO: support multiple qrc{} inputs if/when have a use-case. Note that
//          in such a case the output goes into the same file.
//
// @@ TODO: We could potentially switch to the non-byproduct approach using
//          rcc's --list option (which prints just the resource paths, one per
//          line). Note, however, that it does not distinguish non-existent
//          files (which would be the reason to switch to non-byproduct) so we
//          would have to handle this ourselves (probably by just stat'ing
//          them). Maybe/later.
//
namespace build2
{
  namespace qt
  {
    namespace rcc
    {
      struct compile_rule::match_data
      {
        match_data (const compile_rule& r, size_t pn) : pts_n (pn), rule (r) {}

        depdb::reopen_state dd;

        // The number of valid resource paths read from the depdb.
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

      bool compile_rule::
      match (action a, target& t) const
      {
        tracer trace ("qt::rcc::compile_rule::match");

        // See if we have a .qrc file as prerequisite.
        //
        for (prerequisite_member p: prerequisite_members (a, t))
        {
          if (include (a, t, p) != include_type::normal) // Excluded/ad hoc.
            continue;

          if (p.is_a<qrc> ())
            return true;
        }

        l4 ([&]{trace << "no resource collection file for target " << t;});
        return false;
      }

      recipe compile_rule::
      apply (action a, target& xt) const
      {
        tracer trace ("qt::rcc::compile_rule::apply");

        file& t (xt.as<file> ());
        const path& tp (t.derive_path ());

        context& ctx (t.ctx);
        const scope& bs (t.base_scope ());

        // Inject dependency on the output directory.
        //
        const fsdir* dir (inject_fsdir (a, t));

        // For update inject dependency on the RCC compiler target.
        //
        if (a == perform_update_id)
          inject (a, t, ctgt);

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

        // Get the qrc{} prerequisite target.
        //
        const qrc* s;
        {
          for (prerequisite_target& p: t.prerequisite_targets[a])
            if ((s = p->is_a<qrc> ()))
              break;
        }

        // Create the output directory.
        //
        if (dir != nullptr)
          fsdir_rule::perform_update_direct (a, *dir);

        // We use depdb to track changes to the .qrc file name, options,
        // compiler, etc.
        //
        // We also track the set of resource paths declared in the qrc{} file,
        // excluding those that are static prerequisites (probably generated
        // resources). We validate the resource paths now (during match) but
        // write them only after rcc has been run because the dynamic
        // dependencies are extracted as a byproduct of recipe execution.
        //
        // @@ TMP It took me a while to remember why generated resources need
        //    to be declared as static prerquisites so thought it would help
        //    to explain it more explicitly.
        //
        // Note that it is due to the use of the byproduct approach that
        // generated resources have to be declared as static prerequisites of
        // the rcc output target otherwise they will not exist when rcc is run
        // for the first time (ever, or after a new generated resource was
        // added to the build) and thus rcc would keep failing.
        //
        depdb dd (tp + ".d");
        {
          // First should come the rule name/version.
          //
          if (dd.expect ("qt.rcc.compile 1") != nullptr)
            l4 ([&]{trace << "rule mismatch forcing update of " << t;});

          // Then the compiler checksum.
          //
          if (dd.expect (csum) != nullptr)
            l4 ([&]{trace << "compiler mismatch forcing update of " << t;});

          // Then the options checksum.
          //
          {
            sha256 cs;
            append_options (cs, t, "qt.rcc.options");

            if (dd.expect (cs.string ()) != nullptr)
              l4 ([&]{trace << "options mismatch forcing update of " << t;});
          }

          // Finally the .qrc input file.
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

        // Update the static prerequisites (including the qrc{} input and,
        // possibly, generated resources).
        //
        // Note that, if the qrc{} input has changed, its set of declared
        // resource paths is likely to have changed, otherwise the entire set
        // is still valid (a resource cannot include other resources).
        // Therefore resource states have no bearing on the set of resource
        // paths in the depdb and thus they strictly speaking don't need to be
        // updated before checking the depdb. However we do update them now
        // for the sake of simplicity and consistency with the general
        // approach of using the dyndep_rule mechanisms.
        //
        {
          auto& pts (t.prerequisite_targets[a]);

          for (prerequisite_target& p: pts)
            u = update (trace, a, *p.target, u ? timestamp_unknown : mt) || u;
        }

        match_data md (*this, t.prerequisite_targets[a].size ());

        // Verify the resource paths in the depdb unless we're already
        // updating (in which case they will be overwritten in
        // perform_update()).
        //
        if (!u)
        {
          // Find or enter a resource as a target, update it, and inject it as
          // a prerequisite target.
          //
          // Return true if the resource has changed and nullopt if it does
          // not exist.
          //
          auto add = [&trace, a, &bs, &t, mt] (path fp) -> optional<bool>
          {
            if (const build2::file* ft = enter_file (
                  trace, "resource file",
                  a, bs, t,
                  fp, true /* cache */, true /* normalized */,
                  nullptr /* map_ext */, file::static_type).first)
            {
              // Note that static prerequisites are never written to the
              // depdb (see above).
              //
              if (optional<bool> u = inject_existing_file (
                    trace, "resource file",
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

          // Read the resource paths from the depdb.
          //
          while (!u)
          {
            // We should always end with a blank line.
            //
            string* l (dd.read ());

            // If the line is invalid, run rcc.
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
              // Count valid resource path lines so that, if we encounter an
              // invalid one, we know how many to skip when updating the depdb
              // from rcc's depfile later.
              //
              md.skip_count++;

              if (*r)
                u = true;
            }
            else
            {
              // Resource does not exist. Invalidate this line and trigger
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
        tracer trace ("qt::rcc::compile_rule::perform_update");

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
        const qrc* s;
        {
          // Note: while strictly speaking we don't need the mtime check, this
          // is the most convenient version of execute_prerequisites().
          //
          auto pr (execute_prerequisites<qrc> (a, t, md.mt));

          if (pr.first)
            return *pr.first; // No need to update.

          assert (md.mt == timestamp_nonexistent);
          s = &pr.second;
        }

        // Prepare the rcc command line.
        //
        const process_path& pp (ctgt.process_path ());
        cstrings args {pp.recall_string ()};

        append_options (args, t, "qt.rcc.options");

        // --name <name> Create an external initialization function with
        //               <name>.
        //
        // The convention seems to be to use the .qrc file name for <name> so
        // do that if the user didn't pass the option.
        //
        // Although --name is optional, none of the Qt resource infrastructure
        // seems to support its absence so it's effectively required. Note,
        // however, that it is only actually used for resources embedded in a
        // static library (as an argument to the Q_INIT_RESOURCE() macro).
        //
        if (!find_options ({"--name", "-name"}, args) &&
            !find_option_prefixes ({"--name=", "-name="}, args))
        {
          // Note that rcc will sanitize the name if necessary.
          //
          args.push_back ("--name");
          args.push_back (s->name.c_str ());
        }

        // Output and depfile paths. Translate paths to relative (to working
        // directory) for easier to read diagnostics.
        //
        path relo (relative (tp));
        path depfile (relo.string () + ".t");

        args.push_back ("--depfile");
        args.push_back (depfile.string ().c_str ());

        args.push_back ("-o");
        args.push_back (relo.string ().c_str ());

        // Add the qrc{} input path. Pass the absolute path to cause absolute
        // paths to be written to the depfile.
        //
        args.push_back (s->path ().string ().c_str ());

        args.push_back (nullptr);

        if (verb >= 2)
          print_process (args);
        else if (verb)
          print_diag ("rcc", *s, t);

        // Sequence start time for mtime checks below.
        //
        timestamp start (!ctx.dry_run && depdb::mtime_check ()
                         ? system_clock::now ()
                         : timestamp_unknown);

        if (!ctx.dry_run)
          run (ctx, pp, args, 1 /* finish_verbosity */);

        // Write the resource paths contained in the rcc-generated depfile to
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
            // Note that unlike prerequisites, here we don't need
            // normalize_external() since we expect the targets to be within
            // this project.
            //
            try
            {
              fp.normalize ();
            }
            catch (const invalid_path&)
            {
              fail << "invalid resource file target path '"
                   << fp.string () << "'";
            }

            if (const build2::file* ft = find_file (
                trace, "resource file",
                a, bs, t,
                fp, false /* cache */, true /* normalized */,
                true /* dynamic */,
                nullptr /* map_ext */, file::static_type).first)
            {
              // Do not store static resource prerequisites in the depdb.
              //
              {
                auto& pts (t.prerequisite_targets[a]);

                for (size_t i (0); i != pts_n; ++i)
                  if (pts[i].target == ft)
                    return;
              }

              // Skip those resource paths that already exist in the depdb.
              //
              if (skip != 0)
              {
                --skip;
                return;
              }

              // Verify it has noop recipe.
              //
              verify_existing_file (trace, "resource file", a, t, pts_n, *ft);
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
