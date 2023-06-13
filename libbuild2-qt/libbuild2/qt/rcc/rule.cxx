#include <libbuild2/qt/rcc/rule.hxx>

#include <libbuild2/depdb.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/dyndep.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>
#include <libbuild2/make-parser.hxx>

#include <libbuild2/qt/rcc/target.hxx>

// @@ TMP Should we support multiple qrc{} inputs?
//
// @@ TMP We can switch to non-byproduct mode by using rcc's --list option
//        (which prints just the resource paths, one per line) instead of
//        --depfile.
//
namespace build2
{
  using dyndep = dyndep_rule;

  namespace qt
  {
    namespace rcc
    {
      struct compile_rule::match_data
      {
        match_data (const compile_rule& r, size_t pn) : pts_n (pn), rule (r) {}

        // If not nullopt then the depdb contains an invalid set of resource
        // paths.
        //
        optional<depdb::reopen_state> dd;

        // The number of valid resource paths read from the depdb.
        //
        size_t skip_count = 0;

        const size_t pts_n; // Number of static prerequisites.

        const scope* bs;
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
        t.derive_path ();
        const path& tp (t.path ());

        context& ctx (t.ctx);
        const scope& bs (t.base_scope ());

        // Inject dependency on the output directory.
        //
        inject_fsdir (a, t);

        // For update inject dependency on the RCC compiler target.
        //
        if (a == perform_update_id)
          inject (a, t, rcc);

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
        fsdir_rule::perform_update_direct (a, t);

        // We use depdb to track changes to the .qrc file name, options,
        // compiler, etc.
        //
        // We also track the set of resource paths declared in the qrc{}
        // file. We validate the resource paths now (during match) but write
        // them only after rcc has been run because the dynamic dependencies
        // are extracted as a byproduct of recipe execution.
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
        bool update;
        timestamp mt;

        if (dd.writing ())
        {
          update = true;
          mt = timestamp_nonexistent;
        }
        else
        {
          if ((mt = t.mtime ()) == timestamp_unknown)
            t.mtime (mt = mtime (tp));

          update = dd.mtime > mt;
        }

        // Update the qrc{} prerequisite target. If it has changed, the set of
        // declared resource paths is likely to have changed; otherwise the
        // entire set is still valid.
        //
        if (!update)
        {
          update = dyndep::update (
            trace, a, *s, update ? timestamp_unknown : mt) || update;
        }

        match_data md (*this, t.prerequisite_targets[a].size ());

        // Verify the resource paths in the depdb unless we're already
        // updating (in which case they will be verified by rcc when the
        // target is updated).
        //
        if (!update)
        {
          // Find or enter a resource as a target and inject it as a
          // prerequisite target.
          //
          // Return false if the resource path does not exist.
          //
          // Don't update the resource targets now because their states have
          // no bearing on the validity of the set of resource paths in the
          // depdb (a resource cannot include other resources).
          //
          auto add = [&trace, a, &bs, &t] (path fp) -> bool
          {
            // @@ TODO/TMP Instead of returning null, enter_file() fails with
            //             "error: no existing source file for prerequisite"
            //             if the file does not exist (regardless of
            //             extension/type). Same thing in the adhoc rule; the
            //             branch which handles a null return never gets
            //             executed (tested that by adding xxx.h to a Qt .qrc
            //             file and then deleting it).
            //
            if (const build2::file* ft = dyndep::enter_file (
                  trace, "file",
                  a, bs, t,
                  fp, true /* cache */, true /* normalized */,
                  nullptr /* map_ext */, file::static_type).first)
            {
              // Note that static prerequisites are never written to the
              // depdb.

              // Ensure that the resource is not generated and then inject it
              // as a prerequisite target.
              //
              // This code is essentially inject_existing_file() without the
              // update.
              //
              if (try_match_sync (a, *ft).first)
              {
                recipe_function* const* rf (
                  (*ft)[a].recipe.target<recipe_function*> ());

                // @@ TMP Not doing the updated_during_match(a, t, *ft) check
                //        because none of these resources are static
                //        prerequisites.
                //
                if (rf == nullptr || *rf != &noop_action)
                {
                  fail << "resource " << *ft << " has non-noop recipe"
                       << info
                       << "consider listing it as static prerequisite of " << t;
                }

                // Add to our prerequisite target list.
                //
                t.prerequisite_targets[a].emplace_back (ft);

                return true;
              }
            }

            return false;
          };

          auto df = make_diag_frame (
            [&t] (const diag_record& dr)
            {
              if (verb != 0)
                dr << info << "while extracting dynamic dependencies for "
                   << t;
            });

          // Read the resource paths from the depdb.
          //
          for (;;)
          {
            // We should always end with a blank line.
            //
            string* l (dd.read ());

            // If the line is invalid, run rcc.
            //
            if (l == nullptr)
            {
              update = true;
              break;
            }

            if (l->empty ()) // Done, nothing changed.
              break;

            // Count valid resource paths so that, if we encounter an invalid
            // one, we know how many to skip when updating from rcc's depfile
            // later.
            //
            // @@ TMP This (invalid line) is probably rare so perhaps writing
            //        all resource paths every time wouldn't be so bad? In
            //        which case it would be nice if we could take the depdb
            //        reopen state -- without closing it -- right after
            //        reading the qrc path from it which would thus point to
            //        the first resource path. Then we wouldn't need to do any
            //        skipping. But I suppose that goes against the intended
            //        depdb semantics.
            //
            md.skip_count++;

            // If a resource does not exist there is nothing we can do: the
            // set of resource paths is valid (because the qrc{} file has not
            // changed) so rcc will fail. Therefore preserve the set of
            // resource paths by not closing the depdb yet (i.e., read until
            // the end). Note that, although the depdb state will be saved, it
            // will not be read because rcc will fail.
            //
            if (!add (path (*l)))
            {
              update = true;

              diag_record dr;
              dr << error << "resource " << *l << " does not exist";

              // @@ TMP ctx.match_only?
              //
              // @@ TMP ctx.dry_run or ctx.dry_run_option?
              //
              if (!ctx.dry_run)
                dr << info << "failure deferred to rcc diagnostics";
            }
          }
        }

        // Note that during a dry run we may end up with an incomplete (but
        // valid) database, but it will be updated on the next non-dry run.
        //
        if (!update || ctx.dry_run)
          dd.close (false /* mtime_check */);
        else
          md.dd = dd.close_to_reopen ();

        // Pass on base scope and update/mtime.
        //
        md.bs = &bs;
        md.mt = update ? timestamp_nonexistent : mt;

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

        // Update prerequisites and determine if any render us out-of-date.
        //
        const qrc* s;
        {
          // Exclude the qrc{} prerequisite from the mtime check because it
          // was updated in apply().
          //
          auto flt = [] (const target& t, size_t /* pos */)
          {
            return !t.is_a<qrc> ();
          };

          auto pr (execute_prerequisites<qrc> (a, t, md.mt, flt));

          if (pr.first)
            return *pr.first; // No need to update.

          s = &pr.second;
        }

        // Prepare the rcc command line.
        //
        const process_path& pp (rcc.process_path ());
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

        // Only ask for a depfile if the set of resource paths in the depdb
        // are out of date.
        //
        if (md.dd)
        {
          args.push_back ("--depfile");
          args.push_back (depfile.string ().c_str ());
        }

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

        // If we saved the depdb state in apply(), write to it the resource
        // paths contained in the rcc-generated depfile.
        //
        if (!ctx.dry_run && md.dd)
        {
          depdb dd (move (*md.dd));
          size_t skip (md.skip_count);

          // Note that fp is expected to be absolute.
          //
          auto add = [&trace,
                      a, &bs, &t, pts_n = md.pts_n,
                      &dd, &skip] (path fp)
          {
            normalize_external (fp, "file");

            if (const build2::file* ft = dyndep::find_file (
                trace, "file",
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

              // Skip the valid resource path lines written in apply().
              //
              if (skip != 0)
              {
                --skip;
                return;
              }

              // Verify it has noop recipe.
              //
              dyndep::verify_existing_file (trace, "file", a, t, pts_n, *ft);
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

          md.dd->path = move (dd.path); // For mtime check below.
        }

        timestamp now (system_clock::now ());

        if (!ctx.dry_run && md.dd)
          depdb::check_mtime (start, md.dd->path, t.path (), now);

        t.mtime (now);

        return target_state::changed;
      }
    }
  }
}
