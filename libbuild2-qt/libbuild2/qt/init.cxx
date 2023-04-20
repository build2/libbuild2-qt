#include <libbuild2/qt/init.hxx>

#include <libbuild2/scope.hxx>
#include <libbuild2/variable.hxx>

#include <libbuild2/qt/moc/module.hxx>
#include <libbuild2/qt/rcc/module.hxx>
#include <libbuild2/qt/uic/module.hxx>

namespace build2
{
  namespace qt
  {
    // Get and check the value of `qt.version` (defined before the using
    // directive), enter the variable, and return its value.
    //
    static uint64_t
    check_version (scope& s, const location& loc)
    {
      variable_pool& vp (s.var_pool (true /* public */));
      const variable& var (vp.insert<uint64_t> ("qt.version"));

      lookup l (s[var]);
      if (!l)
        fail (loc) << "define " << var.name << " before the using directive";

      return cast<uint64_t> (l);
    }

    // The `qt` module.
    //
    bool
    qt_init (scope& rs,
             scope& bs,
             const location& loc,
             bool,
             bool,
             module_init_extra& extra)
    {
      load_module (rs, bs, "qt.moc", loc, extra.hints);
      load_module (rs, bs, "qt.rcc", loc, extra.hints);
      load_module (rs, bs, "qt.uic", loc, extra.hints);

      return true;
    }

    // The `qt.moc.guess` module.
    //
    bool
    moc_guess_init (scope& /*rs*/,
                    scope& bs,
                    const location& loc,
                    bool,
                    bool,
                    module_init_extra& extra)
    {
      using namespace moc;

      extra.set_module (new module (data {check_version (bs, loc)}));

      return true;
    }

    // The `qt.moc.config` module.
    //
    bool
    moc_config_init (scope& rs,
                     scope& bs,
                     const location& loc,
                     bool,
                     bool,
                     module_init_extra& extra)
    {
      using namespace moc;

      // Load qt.moc.guess and share its module instance as ours.
      //
      extra.module = load_module (rs, bs, "qt.moc.guess", loc, extra.hints);

      return true;
    }

    // The `qt.moc` module.
    //
    bool
    moc_init (scope& rs,
              scope& bs,
              const location& loc,
              bool,
              bool,
              module_init_extra& extra)
    {
      using namespace moc;

      // Load qt.moc.config and share its module instance as ours.
      //
      extra.module = load_module (rs, bs, "qt.moc.config", loc, extra.hints);

      return true;
    }

    // The `qt.rcc.guess` module.
    //
    bool
    rcc_guess_init (scope& /*rs*/,
                    scope& bs,
                    const location& loc,
                    bool,
                    bool,
                    module_init_extra& extra)
    {
      using namespace rcc;

      extra.set_module (new module (data {check_version (bs, loc)}));

      return true;
    }

    // The `qt.rcc.config` module.
    //
    bool
    rcc_config_init (scope& rs,
                     scope& bs,
                     const location& loc,
                     bool,
                     bool,
                     module_init_extra& extra)
    {
      using namespace rcc;

      // Load qt.rcc.guess and share its module instance as ours.
      //
      extra.module = load_module (rs, bs, "qt.rcc.guess", loc, extra.hints);

      return true;
    }

    // The `qt.rcc` module.
    //
    bool
    rcc_init (scope& rs,
              scope& bs,
              const location& loc,
              bool,
              bool,
              module_init_extra& extra)
    {
      using namespace rcc;

      // Load qt.rcc.config and share its module instance as ours.
      //
      extra.module = load_module (rs, bs, "qt.rcc.config", loc, extra.hints);

      return true;
    }

    // The `qt.uic.guess` module.
    //
    bool
    uic_guess_init (scope& /*rs*/,
                    scope& bs,
                    const location& loc,
                    bool,
                    bool,
                    module_init_extra& extra)
    {
      using namespace uic;

      extra.set_module (new module (data {check_version (bs, loc)}));

      return true;
    }

    // The `qt.uic.config` module.
    //
    bool
    uic_config_init (scope& rs,
                     scope& bs,
                     const location& loc,
                     bool,
                     bool,
                     module_init_extra& extra)
    {
      using namespace uic;

      // Load qt.uic.guess and share its module instance as ours.
      //
      extra.module = load_module (rs, bs, "qt.uic.guess", loc, extra.hints);

      return true;
    }

    // The `qt.uic` module.
    //
    bool
    uic_init (scope& rs,
              scope& bs,
              const location& loc,
              bool,
              bool,
              module_init_extra& extra)
    {
      using namespace uic;

      // Load qt.uic.config and share its module instance as ours.
      //
      extra.module = load_module (rs, bs, "qt.uic.config", loc, extra.hints);

      return true;
    }

    static const module_functions mod_functions[] =
    {
      // NOTE: don't forget to also update the documentation in init.hxx if
      //       changing anything here.

      {"qt",            nullptr, qt_init},
      {"qt.moc.guess",  nullptr, moc_guess_init},
      {"qt.moc.config", nullptr, moc_config_init},
      {"qt.moc",        nullptr, moc_init},
      {"qt.rcc.guess",  nullptr, rcc_guess_init},
      {"qt.rcc.config", nullptr, rcc_config_init},
      {"qt.rcc",        nullptr, rcc_init},
      {"qt.uic.guess",  nullptr, uic_guess_init},
      {"qt.uic.config", nullptr, uic_config_init},
      {"qt.uic",        nullptr, uic_init},
      {nullptr,         nullptr, nullptr}
    };

    const module_functions*
    build2_qt_load ()
    {
      return mod_functions;
    }
  }
}
