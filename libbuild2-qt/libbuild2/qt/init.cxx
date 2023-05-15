#include <libbuild2/qt/init.hxx>

#include <libbuild2/file.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/variable.hxx>

#include <libbuild2/qt/moc/module.hxx>
#include <libbuild2/qt/rcc/module.hxx>
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
    static const exe*
    import_exe (scope& rs,
                const char* name,   // Compiler name (`moc`/`rcc`/`uic`)
                uint64_t ver,       // Qt version.
                const location& loc,
                bool opt)
    {
      import_result<exe> ir;
      bool new_cfg (false);
      {
        // Import the project-qualified target name (e.g.,
        // Qt5Moc%exe{qt5moc}).
        //
        // Version string, exe name, project name.
        //
        string vs (to_string (ver));
        string en ("qt" + vs + name);                         // `qt5moc`
        string pn ("Qt" + vs + ucase (name[0]) + (name + 1)); // `Qt5Moc`

        ir = import_direct<exe> (
          new_cfg,
          rs,
          build2::name (move (pn), dir_path (), "exe", move (en)),
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

      // Print the report.
      //
      // If this is a configuration with new values, then print the report
      // at verbosity level 2 and up (-v).
      //
      if (verb >= (new_cfg ? 2 : 3))
      {
        diag_record dr (text);
        dr << "qt." << name << " " << project (rs) << '@' << rs << '\n';

        if (ir.target != nullptr)
          dr << "  " << name << "        " << ir << '\n';
        else
          dr << "  " << name << "        not found, leaving unconfigured";
      }

      return ir.target;
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

      uint64_t v (check_version (bs, loc, first));
      if (first)
      {
        const exe* moc (import_exe (rs, "moc", v, loc, opt));

        if (moc == nullptr)
          return false;

        extra.set_module (new module (data {v, *moc}));
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

      if (opt)
        fail (loc) << "qt.moc.config does not support optional loading";

      // Load qt.moc.guess and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.moc.guess", loc, extra.hints));

        if (first)
          extra.module = move (m);
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

      uint64_t v (check_version (bs, loc, first));
      if (first)
      {
        const exe* rcc (import_exe (rs, "rcc", v, loc, opt));

        if (rcc == nullptr)
          return false;

        extra.set_module (new module (data {v, *rcc}));
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

      if (opt)
        fail (loc) << "qt.rcc.config does not support optional loading";

      // Load qt.rcc.guess and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.rcc.guess", loc, extra.hints));

        if (first)
          extra.module = move (m);
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

      if (opt)
        fail (loc) << "qt.rcc does not support optional loading";

      // Load qt.rcc.config and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.rcc.config", loc, extra.hints));

        if (first)
          extra.module = move (m);
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

      uint64_t v (check_version (bs, loc, first));

      if (first)
      {
        const exe* uic (import_exe (rs, "uic", v, loc, opt));

        if (uic == nullptr)
          return false;

        extra.set_module (new module (data {v, *uic}));
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

      if (opt)
        fail (loc) << "qt.uic.config does not support optional loading";

      // Load qt.uic.guess and share its module instance as ours.
      //
      {
        auto m (load_module (rs, bs, "qt.uic.guess", loc, extra.hints));

        if (first)
          extra.module = move (m);
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
