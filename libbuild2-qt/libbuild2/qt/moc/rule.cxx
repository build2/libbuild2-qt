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
#include <libbuild2/bin/utility.hxx>

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

        strings lib_opts; // Prerequisite library options.

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

      // Return true if the specified class of options should be passed to
      // moc. Valid option classes are `poptions`, `predefs`, and
      // `sys_hdr_dirs`. Each option class is associated with a variable named
      // `qt.moc.auto_<class>`.
      //
      static bool pass_moc_opts (const target& t, const char* oc)
      {
        // Fall back to qt.moc.auto_preprocessor if the variable is null or
        // undefined.
        //
        lookup l (t[string ("qt.moc.auto_") + oc]);
        return l ? cast<bool> (l)
                 : cast_true<bool> (t["qt.moc.auto_preprocessor"]);
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
        const fsdir* dir (inject_fsdir (a, t));

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
        // The purpose of library prerequisites is to get their library
        // metadata (exported options such as macro definitions, header search
        // directories, etc.) to be passed to moc.
        //
        auto& pts (t.prerequisite_targets[a]);
        {
          optional<dir_paths> usr_lib_dirs; // Extract lazily.

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

              // Fail if this is a lib{} because we cannot possibly pick a
              // member and matching the group will most likely produce an
              // undesirable result (unmatch will fail, we will build both
              // member, etc).
              //
              // The only sensible way out of this rabbit hole seems to be to
              // require the user to "signal" what will be used by going
              // through a utility library (either libul{} or libue{}).
              //
              if (p.is_a<bin::lib> ())
              {
                fail << "unable to extract preprocessor options for "
                     << t << " from " << p << " directly" <<
                  info << "instead go through a \"metadata\" utility library "
                     << "(either libul{} or libue{})" <<
                  info << "see qt.moc module documentation for details";
              }

              // Handle (phase two) imported libraries.
              //
              if (p.proj ())
              {
                pt = cxx_mod->search_library (a,
                                              cxx_mod->sys_lib_dirs,
                                              usr_lib_dirs,
                                              p.prerequisite);
              }

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
        if (dir != nullptr)
          fsdir_rule::perform_update_direct (a, *dir);

        match_data md (*this, s, t.prerequisite_targets[a].size ());

        // Get prerequisite library options for change tracking saving them in
        // match_data for reuse in perform_update().
        //
        for (size_t i (0); i != md.pts_n; ++i)
        {
          prerequisite_target p (pts[i]);

          if (p.adhoc ())
            continue;

          // The prerequisite's target. Unmatched library targets were moved
          // to the data member during match.
          //
          if (const target* pt =
              (p.include & prerequisite_target::include_target) == 0
              ? p.target
              : reinterpret_cast<target*> (p.data))
          {
            using namespace bin;

            bool la (false); // True if this is a static library.

            // Skip if this is not a library. (Note that this cannot be a
            // lib{} because those are rejected during match.)
            //
            if ((     pt->is_a<libs>())  ||
                (la = pt->is_a<liba>())  ||
                (la = pt->is_a<libul>()) ||
                (la = pt->is_a<libux>()))
            {
              // If this is libul{}, get the matched member (see
              // bin::libul_rule for details).
              //
              const file& f ((pt->is_a<libul> ()
                              ? pt->prerequisite_targets[a].back ().target
                              : pt)->as<file> ());

              cc::compile_rule::appended_libraries ls;

              // Pass true for `common` in order to get just the common
              // interface options if possible, and true for `original` in
              // order not to translate -I to -isystem.
              //
              cxx_mod->append_library_options (
                ls,
                md.lib_opts,
                bs,
                a, f, la,
                link_info (bs, link_type (f).type),
                true /* common */,
                true /* original */);
            }
          }
        }

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

            // Note: see below for the order.
            //
            append_options (cs, t, "qt.moc.options");

            // Include cc.poptions and cxx.poptions.
            //
            if (pass_moc_opts (t, "poptions"))
            {
              append_options (cs, t, cxx_mod->c_poptions);
              append_options (cs, t, cxx_mod->x_poptions);
            }

            // Include prerequisite library options in the checksum.
            //
            append_options (cs, md.lib_opts);

            // Include the system header directory paths in the checksum.
            //
            if (pass_moc_opts (t, "sys_hdr_dirs"))
            {
              for (const dir_path& d: cxx_mod->sys_hdr_dirs)
                append_option (cs, d.string ().c_str ());
            }

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
          if (((p.include & include_unmatch) != 0) || is_lib (p->type ()))
            continue;

          u = update (trace, a, *p.target, u ? timestamp_unknown : mt) || u;
        }

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
          auto add = [&trace, a, &bs, &t, mt, pts_n = md.pts_n] (path fp)
            -> optional<bool>
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
              if (optional<bool> u = inject_existing_file (trace, "header",
                                                           a, t, pts_n,
                                                           *ft, mt,
                                                           false /* fail */))
              {
                return u;
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

        // The correct order of options is as follows:
        //
        // 1. predefs (via --include; still @@ TODO)
        // 2. qt.moc.options
        // 3. project poptions (cc.poptions, cxx.poptions)
        // 4. library poptions (*.export.poptions)
        // 5. sys_hdr_dirs
        //
        append_options (args, t, "qt.moc.options");

        // Add cc.poptions, cxx.poptions, prerequisite library options, and
        // -I's for the system header directories.
        //
        if (pass_moc_opts (t, "poptions"))
        {
          append_options (args, t, cxx_mod->c_poptions);
          append_options (args, t, cxx_mod->x_poptions);
        }

        for (const string& o: md.lib_opts)
          args.push_back (o.c_str ());

        if (pass_moc_opts (t, "sys_hdr_dirs"))
        {
          for (const dir_path& d: cxx_mod->sys_hdr_dirs)
          {
            args.push_back ("-I");
            args.push_back (d.string ().c_str ());
          }
        }

        // The value to be passed via the -f option: the bracket- or
        // quote-enclosed source file include path, e.g., `<moc/source.hxx>`.
        //
        // Note: only used if the output is a cxx{}.
        //
        string popt_val;

        // Parse the -p option (source file include prefix) passed in
        // qt.moc.options. While at it, also validate there are no -i or -f.
        //
        // Keep the value from the last -p instance. Remove all the -p
        // instances, including the last, from args because the prefix will be
        // incorporated in the value passed to moc via the -f option (see
        // below).
        //
        // Note: we do this even if the target is moc{} (and thus nothing is
        // included) because the qt.moc.options value is usually common for
        // the entire project (this is in a sense parallel to the moc's
        // behavior which will ignore -p with -i).
        //
        // Variations accepted by moc (see QCommandLineParser):
        //
        // -p X
        // -pX
        // --p X
        // --p=X
        //
        // Note also that strictly speaking we can mis-treat an option value
        // as the option name. However, in this case, chances of a value
        // starting with `-` are quite remote and the user can always work
        // around it by using the --<opt>=<val> form (e.g., --x=-p).
        //
        for (auto i (args.begin () + 1); i != args.end (); )
        {
          // Check if we are looking at a one-letter option dealing with the
          // various possible forms and return nullopt if that's not the
          // case. If it is the case and val is false (option has no value),
          // return NULL. Otherwise, return the beginning of the option value
          // (issuing diagnostics and failing if the value is missing).
          //
          // In all cases, if not returning nullopt, erase the option and
          // potentially separate value from args.
          //
          auto opt = [&args, &i] (const char n, bool val) ->
            optional<const char*>
          {
            const char* a (*i);

            if (a[0] != '-') // Not an option.
              return nullopt;

            // Skip the option name if found, otherwise return nullopt.
            //
            size_t p;
            if (a[1] == n)
              p = 2;
            else if (a[1] == '-' && a[2] == n)
              p = 3;
            else
              return nullopt;

            const char* v (nullptr); // Option value.

            if (a[p] == '\0') // -p X | --p X
            {
              if (val)
              {
                i = args.erase (i); // Option.

                if (i == args.end ())
                  fail << "qt.moc.options contains " << a
                       << " option without value";

                v = *i;
                i = args.erase (i); // Value.
              }
              else
                i = args.erase (i); // Option.
            }
            else // -pX | --p=X
            {
              if (p == 3) // --p=X
              {
                if (a[p] == '=')
                  ++p;
                else
                  return nullopt; // E.g., --print.
              }

              v = a + p;
              i = args.erase (i);
            }

            return v;
          };

          if (opt ('i', false))
          {
            fail << "qt.moc.options contains -i option" <<
              info << "use moc{} target if compiling source file";
          }

          if (opt ('f', true))
          {
            fail << "qt.moc.options contains -f option" <<
              info << "use -p to specify custom prefix" <<
              info << "use qt.moc.include_with_quotes to include with quotes";
          }

          if (optional<const char*> v = opt ('p', true))
            popt_val = *v;
          else
            ++i;
        }

        // If we're generating a cxx{}, pass -f to override the path with
        // which the input header will be #include'd (which is relative to the
        // output directory by default) with the brackets or quotes and,
        // optionally, path-prefixed header file name. The prefix is moved
        // from the -p option because currently the only way of controlling
        // the quoting style (<> vs. "") is via the -f option.
        //
        // Otherwise, if we're generating a moc{} -- which is included -- pass
        // -i to prevent any #include directive from being generated for the
        // input source file (otherwise we'd get multiple definitions errors
        // if the input source file is also compiled, as is typical).
        //
        string fopt_val;

        if (t.is_a<cxx> ())
        {
          // Goal: something like `-f <hello/hello.hxx>`.
          //
          bool q (cast_false<bool> (t["qt.moc.include_with_quotes"]));

          fopt_val += (q ? '"' : '<');
          if (!popt_val.empty ())
          {
            fopt_val += popt_val;
            fopt_val += '/';
          }
          fopt_val += sp.leaf ().string ();
          fopt_val += (q ? '"' : '>');

          args.push_back ("-f");
          args.push_back (fopt_val.c_str ());
        }
        else if (t.is_a<moc> ())
          args.push_back ("-i");

        // Translate output path to relative (to working directory) for easier
        // to read diagnostics. The input path, however, must be absolute
        // otherwise moc will put the relative path in the depfile.
        //
        path relo (relative (tp));            // Output path.
        path depfile (relo.string () + ".t"); // Depfile path.

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
              // Do not store the target itself in the depdb. This happens
              // when moc doesn't realise that its input file is including its
              // output file and then declares the latter as a prerequisite of
              // itself. (Note that for this to work the target must come
              // before the prerequisites in the depfile.)
              //
              if (ft == &t)
                return;

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
