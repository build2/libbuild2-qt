#include <libbuild2/qt/init.hxx>

#include <libbuild2/file.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/variable.hxx>

#include <libbuild2/config/utility.hxx>

#include <libbuild2/qt/moc/module.hxx>

#include <libbuild2/qt/rcc/module.hxx>
#include <libbuild2/qt/rcc/target.hxx>

#include <libbuild2/qt/uic/module.hxx>

namespace build2
{
  namespace qt
  {
    // Enter the `qt.version` variable, get its values, verify correct,
    // and return it.
    //
    static uint64_t
    check_version (scope& bs, const location& loc, bool first)
    {
      // The variable that we enter is qualified so go straight for the public
      // variable pool.
      //
      variable_pool& vp (bs.var_pool (true /* public */));

      //-
      //     qt.version [uint64]
      //
      // The Qt version being used. Must be set before loading any of the
      // `qt` modules. Valid values are 5 and 6.
      //
      //-
      const variable& var (first
                           ? vp.insert<uint64_t> ("qt.version")
                           : *vp.find ("qt.version"));

      if (const uint64_t* v = cast_null<uint64_t> (bs[var]))
      {
        if (*v < 5 || *v > 6)
          fail (loc) << "invalid " << var << " value " << *v << endf;

        return *v;
      }
      else
        fail (loc) << "set " << var << " before the using directive" << endf;
    }

    // Import a Qt compiler and print the configuration report.
    //
    // Note that the compiler name is currently assumed to match the module
    // name (e.g., `moc` and `qt.moc`).
    //
    // Return the compiler target in first and the compiler checksum in
    // second.
    //
    static pair<const exe*, const string*>
    import_exe (scope& rs,
                const string& name, // Compiler name (`moc`/`rcc`/`uic`).
                uint64_t qt_ver,    // Qt version (major).
                const location& loc,
                bool opt)
    {
      string exe_name ("qt" + to_string (qt_ver) + name); // `qt5moc`

      // Enter variables.
      //
      // They are all qualified so go straight for the public variable pool.
      //
      // The qt.{moc,rcc,uic} variables (untyped) are the imported compiler
      // target name.
      //
      variable_pool& vp (rs.var_pool (true /* public */));

      auto& v_tgt (vp.insert ("qt." + name));
      auto& v_ver (vp.insert<string> ("qt." + name + ".version"));
      auto& v_sum (vp.insert<string> ("qt." + name + ".checksum"));

      // Import the compiler target.
      //
      import_result<exe> ir;
      bool new_cfg (false);
      {
        // Import the project-qualified target name (e.g.,
        // Qt5Moc%exe{qt5moc}).
        //

        // Project name (e.g., `Qt5Moc`).
        //
        string pn ("Qt" + to_string (qt_ver) + ucase (name[0]) + &name[1]);

        ir = import_direct<exe> (
          new_cfg,
          rs,
          build2::name (move (pn), dir_path (), "exe", exe_name),
          true, // phase2
          opt,
          true, // metadata
          loc,
          "module load");

        // @@ TODO: maybe/later fallback to system-installed upstream names
        //    (`moc`/`rcc`/`uic`). To do this properly we will need to import
        //    without metadata and then extract it in an ad hoc way (e.g., by
        //    running the executable with --version, etc).
      }

      const exe* tgt (ir.target);

      // Extract metadata.
      //
      // @@ TODO: moc also has environment: need to incorporate this into
      //          change tracking/hermetic.
      //
      auto* ver (tgt != nullptr
                     ? &cast<string> (tgt->vars[exe_name + ".version"])
                     : nullptr);
      auto* sum (tgt != nullptr
                     ? &cast<string> (tgt->vars[exe_name + ".checksum"])
                     : nullptr);

      // Print the report.
      //
      // If this is a configuration with new values, then print the report
      // at verbosity level 2 and up (-v).
      //
      if (verb >= (new_cfg ? 2 : 3))
      {
        diag_record dr (text);
        dr << "qt." << name << " " << project (rs) << '@' << rs << '\n';

        if (tgt != nullptr)
          dr << "  " << name << "        " << ir << '\n'
             << "  version    "            << *ver << '\n'
             << "  checksum   "            << *sum;
        else
          dr << "  " << name << "        not found, leaving unconfigured";
      }

      if (tgt != nullptr)
      {
        rs.assign (v_tgt) = move (ir.name);
        rs.assign (v_sum) = *sum;
        rs.assign (v_ver) = *ver;

        {
          standard_version v (*ver);

          rs.assign<uint64_t> ("qt." + name + ".version.number") = v.version;
          rs.assign<uint64_t> ("qt." + name + ".version.major") = v.major ();
          rs.assign<uint64_t> ("qt." + name + ".version.minor") = v.minor ();
          rs.assign<uint64_t> ("qt." + name + ".version.patch") = v.patch ();
        }
      }
      else
        rs.assign (v_tgt) = nullptr; // More direct indication.

      return make_pair (tgt, sum);
    }

    // The `qt.moc.guess` module.
    //
    bool
    moc_guess_init (scope& rs,
                    scope& bs,
                    const location& loc,
                    bool first,
                    bool opt,
                    module_init_extra& extra)
    {
      using namespace moc;

      tracer trace ("qt::moc_guess_init");
      l5 ([&] { trace << "for " << bs; });

      // Adjust module config.build save priority (code generator).
      //
      config::save_module (rs, "qt.moc", 150);

      uint64_t v (check_version (bs, loc, first));
      if (first)
      {
        auto pr (import_exe (rs, "moc", v, loc, opt));

        const exe* moc (pr.first);
        const string* csum (pr.second);

        if (moc == nullptr)
          return false;

        extra.set_module (new module (data {v, *moc, *csum}));
      }
      else
      {
        module& m (extra.module_as<module> ());

        if (v != m.version)
          fail (loc) << "inconsistent qt.version value " << v << info
                     << "previous value " << m.version;
      }

      return true;
    }

    // The `qt.moc.config` module.
    //
    bool
    moc_config_init (scope& rs,
                     scope& bs,
                     const location& loc,
                     bool first,
                     bool opt,
                     module_init_extra& extra)
    {
      using namespace moc;

      tracer trace ("qt::moc_config_init");
      l5 ([&] { trace << "for " << bs; });

      if (opt)
        fail (loc) << "qt.moc.config does not support optional loading";

      // Load qt.moc.guess and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.moc.guess", loc, extra.hints));

        if (first)
          extra.module = move (m);
      }

      // Configuration.
      //
      if (first)
      {
        // config.qt.moc.options
        //
        // Note that we merge it into the corresponding qt.moc.* variable.
        //
        config::append_config<strings> (rs, rs, "qt.moc.options", nullptr);
      }

      return true;
    }

    // The `qt.moc` module.
    //
    bool
    moc_init (scope& rs,
              scope& bs,
              const location& loc,
              bool first,
              bool opt,
              module_init_extra& extra)
    {
      using namespace moc;

      tracer trace ("qt::moc_init");
      l5 ([&] { trace << "for " << bs; });

      if (opt)
        fail (loc) << "qt.moc does not support optional loading";

      // Load qt.moc.config and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.moc.config", loc, extra.hints));

        if (first)
          extra.module = move (m);
      }

      return true;
    }

    // The `qt.rcc.guess` module.
    //
    bool
    rcc_guess_init (scope& rs,
                    scope& bs,
                    const location& loc,
                    bool first,
                    bool opt,
                    module_init_extra& extra)
    {
      using namespace rcc;

      tracer trace ("qt::rcc_guess_init");
      l5 ([&] { trace << "for " << bs; });

      // Adjust module config.build save priority (code generator).
      //
      config::save_module (rs, "qt.rcc", 150);

      uint64_t v (check_version (bs, loc, first));
      if (first)
      {
        auto pr (import_exe (rs, "rcc", v, loc, opt));

        const exe* rcc (pr.first);
        const string* csum (pr.second);

        if (rcc == nullptr)
          return false;

        extra.set_module (new module (data {v, *rcc, *csum}));
      }
      else
      {
        module& m (extra.module_as<module> ());

        if (v != m.version)
          fail (loc) << "inconsistent qt.version value " << v << info
                     << "previous value " << m.version;
      }

      return true;
    }

    // The `qt.rcc.config` module.
    //
    bool
    rcc_config_init (scope& rs,
                     scope& bs,
                     const location& loc,
                     bool first,
                     bool opt,
                     module_init_extra& extra)
    {
      using namespace rcc;

      tracer trace ("qt::rcc_config_init");
      l5 ([&] { trace << "for " << bs; });

      if (opt)
        fail (loc) << "qt.rcc.config does not support optional loading";

      // Load qt.rcc.guess and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.rcc.guess", loc, extra.hints));

        if (first)
          extra.module = move (m);
      }

      // Configuration.
      //
      if (first)
      {
        // config.qt.rcc.options
        //
        // Note that we merge it into the corresponding qt.rcc.* variable.
        //
        config::append_config<strings> (rs, rs, "qt.rcc.options", nullptr);
      }

      return true;
    }

    // The `qt.rcc` module.
    //
    bool
    rcc_init (scope& rs,
              scope& bs,
              const location& loc,
              bool first,
              bool opt,
              module_init_extra& extra)
    {
      using namespace rcc;

      tracer trace ("qt::rcc_init");
      l5 ([&] { trace << "for " << bs; });

      if (opt)
        fail (loc) << "qt.rcc does not support optional loading";

      // Load qt.rcc.config and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.rcc.config", loc, extra.hints));

        if (first)
          extra.module = move (m);
      }

      // Register target type and rules.
      //
      if (first)
      {
        module& m (extra.module_as<module> ());

        //-
        // Target types:
        //
        //   `qrc{}` -- Qt resource collection file.
        //-
        rs.insert_target_type<qrc> ();

        //-
        // Rules:
        //
        //   `qt.rcc.compile` -- Compile a Qt resource collection file
        //                       identified as the first `qrc{}` prerequisite.
        //-
        rs.insert_rule<file> (perform_update_id,   "qt.rcc.compile", m);
        rs.insert_rule<file> (perform_clean_id,    "qt.rcc.compile", m);
        rs.insert_rule<file> (configure_update_id, "qt.rcc.compile", m);
      }

      return true;
    }

    // The `qt.uic.guess` module.
    //
    bool
    uic_guess_init (scope& rs,
                    scope& bs,
                    const location& loc,
                    bool first,
                    bool opt,
                    module_init_extra& extra)
    {
      using namespace uic;

      tracer trace ("qt::uic_guess_init");
      l5 ([&] { trace << "for " << bs; });

      // Adjust module config.build save priority (code generator).
      //
      config::save_module (rs, "qt.uic", 150);

      uint64_t v (check_version (bs, loc, first));

      if (first)
      {
        auto pr (import_exe (rs, "uic", v, loc, opt));

        const exe* uic (pr.first);
        const string* csum (pr.second);

        if (uic == nullptr)
          return false;

        extra.set_module (new module (data {v, *uic, *csum}));
      }
      else
      {
        module& m (extra.module_as<module> ());

        if (v != m.version)
          fail (loc) << "inconsistent qt.version value " << v << info
                     << "previous value " << m.version;
      }

      return true;
    }

    // The `qt.uic.config` module.
    //
    bool
    uic_config_init (scope& rs,
                     scope& bs,
                     const location& loc,
                     bool first,
                     bool opt,
                     module_init_extra& extra)
    {
      using namespace uic;

      tracer trace ("qt::uic_config_init");
      l5 ([&] { trace << "for " << bs; });

      if (opt)
        fail (loc) << "qt.uic.config does not support optional loading";

      // Load qt.uic.guess and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.uic.guess", loc, extra.hints));

        if (first)
          extra.module = move (m);
      }

      // Configuration.
      //
      if (first)
      {
        // config.qt.uic.options
        //
        // Note that we merge it into the corresponding qt.uic.* variable.
        //
        config::append_config<strings> (rs, rs, "qt.uic.options", nullptr);
      }

      return true;
    }

    // The `qt.uic` module.
    //
    bool
    uic_init (scope& rs,
              scope& bs,
              const location& loc,
              bool first,
              bool opt,
              module_init_extra& extra)
    {
      using namespace uic;

      tracer trace ("qt::uic_init");
      l5 ([&] { trace << "for " << bs; });

      if (opt)
        fail (loc) << "qt.uic does not support optional loading";

      // Load qt.uic.config and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.uic.config", loc, extra.hints));

        if (first)
          extra.module = move (m);
      }

      return true;
    }

    // The `qt` module.
    //
    bool
    qt_init (scope& rs,
             scope& bs,
             const location& loc,
             bool /*first*/,
             bool,
             module_init_extra& extra)
    {
      load_module (rs, bs, "qt.moc", loc, extra.hints);
      load_module (rs, bs, "qt.rcc", loc, extra.hints);
      load_module (rs, bs, "qt.uic", loc, extra.hints);

      return true;
    }

    static const module_functions mod_functions[] =
    {
      // NOTE: don't forget to also update the documentation in init.hxx if
      //       changing anything here.

      {"qt.moc.guess",  nullptr, moc_guess_init},
      {"qt.moc.config", nullptr, moc_config_init},
      {"qt.moc",        nullptr, moc_init},
      {"qt.rcc.guess",  nullptr, rcc_guess_init},
      {"qt.rcc.config", nullptr, rcc_config_init},
      {"qt.rcc",        nullptr, rcc_init},
      {"qt.uic.guess",  nullptr, uic_guess_init},
      {"qt.uic.config", nullptr, uic_config_init},
      {"qt.uic",        nullptr, uic_init},
      {"qt",            nullptr, qt_init},
      {nullptr,         nullptr, nullptr}
    };

    const module_functions*
    build2_qt_load ()
    {
      return mod_functions;
    }
  }
}
