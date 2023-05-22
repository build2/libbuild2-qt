#include <libbuild2/qt/rcc/rule.hxx>

#include <libbuild2/depdb.hxx>
#include <libbuild2/algorithm.hxx>
#include <libbuild2/diagnostics.hxx>

#include <libbuild2/qt/rcc/target.hxx>

namespace build2
{
  namespace qt
  {
    namespace rcc
    {
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

        l4 ([&] { trace << "no resource collection file for target " << t; });
        return false;
      }

      recipe compile_rule::
      apply (action a, target& xt) const
      {
        tracer trace ("qt::rcc::compile_rule::apply");

        file& t (xt.as<file> ());

        t.derive_path ();

        // Inject dependency on the output directory.
        //
        inject_fsdir (a, t);

        // Match prerequisites.
        //
        match_prerequisite_members (a, t);

        // For update inject dependency on the RCC compiler target.
        //
        if (a == perform_update_id)
          inject (a, t, rcc);

        switch (a)
        {
        case perform_update_id: return [this] (action a, const target& t)
          {
            return perform_update (a, t);
          };
        case perform_clean_id:  return &perform_clean_depdb;
        default:                return noop_recipe; // Configure/dist update.
        }
      }

      target_state compile_rule::
      perform_update (action a, const target& xt) const
      {
        tracer trace ("qt::rcc::compile_rule::perform_update");

        context& ctx (xt.ctx);

        const file& t (xt.as<file> ());
        const path& tp (t.path ());

        // Update prerequisites and determine if any render us out-of-date.
        //
        timestamp mt (t.load_mtime ());
        auto pr (execute_prerequisites<qrc> (a, t, mt));

        bool update (!pr.first);
        target_state ts (update ? target_state::changed : *pr.first);

        const qrc& s (pr.second);

        // @@ TODO Track changes to resources with rcc's --depfile option.

        // We use depdb to track changes to the .qrc file name, options,
        // compiler, etc.
        //
        depdb dd (tp + ".d");
        {
          // First should come the rule name/version.
          //
          if (dd.expect ("qt.rcc.compile 1") != nullptr)
            l4 ([&] { trace << "rule mismatch forcing update of " << t; });

          // Then the compiler checksum.
          //
          if (dd.expect (csum) != nullptr)
            l4 ([&] { trace << "compiler mismatch forcing update of " << t; });

          // Then the options checksum.
          //
          sha256 cs;
          append_options (cs, t, "qt.rcc.options");

          if (dd.expect (cs.string ()) != nullptr)
            l4 ([&] { trace << "options mismatch forcing update of " << t; });

          // Finally the .qrc input file.
          //
          if (dd.expect (s.path ()) != nullptr)
            l4 ([&] { trace << "input file mismatch forcing update of " << t; });
        }

        // Update if depdb mismatch.
        //
        if (dd.writing () || dd.mtime > mt)
          update = true;

        dd.close ();

        if (!update)
          return ts;

        // Translate paths to relative (to working directory). This results in
        // easier to read diagnostics.
        //
        path relo (relative (tp));
        path rels (relative (s.path ()));

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
          args.push_back ("--name");
          args.push_back (s.name.c_str ());
        }

        args.push_back ("-o");
        args.push_back (relo.string ().c_str ());

        args.push_back (rels.string ().c_str ());
        args.push_back (nullptr);

        if (verb >= 2)
          print_process (args);
        else if (verb)
          print_diag ("rcc", s, t);

        if (!ctx.dry_run)
        {
          run (ctx, pp, args, 1 /* finish_verbosity */);
          dd.check_mtime (tp);
        }

        t.mtime (system_clock::now ());
        return target_state::changed;
      }
    }
  }
}
