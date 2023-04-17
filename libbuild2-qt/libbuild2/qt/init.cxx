#include <libbuild2/qt/init.hxx>

#include <libbuild2/scope.hxx>

#include <libbuild2/qt/module.hxx>

namespace build2
{
  namespace qt
  {
    //-
    // The `qt.moc.guess` module.
    //-
    bool
    moc_guess_init (scope& /*rs*/,
                    scope& /*bs*/,
                    const location& /*loc*/,
                    bool,
                    bool,
                    module_init_extra& extra)
    {
      extra.set_module (new module_moc ());

      return true;
    }

    //-
    // The `qt.moc.config` module.
    //-
    bool
    moc_config_init (scope& rs,
                     scope& /*bs*/,
                     const location& loc,
                     bool,
                     bool,
                     module_init_extra& extra)
    {
      // Load qt.moc.guess and share its module instance as ours.
      //
      extra.module = load_module (rs, rs, "qt.moc.guess", loc, extra.hints);

      return true;
    }

    //-
    // The `qt.moc` module.
    //-
    bool
    moc_init (scope& rs,
              scope& /*bs*/,
              const location& loc,
              bool,
              bool,
              module_init_extra& extra)
    {
      // Load qt.moc.config and share its module instance as ours.
      //
      extra.module = load_module (rs, rs, "qt.moc.config", loc, extra.hints);

      return true;
    }

    //-
    // The `qt.rcc.guess` module.
    //-
    bool
    rcc_guess_init (scope& /*rs*/,
                    scope& /*bs*/,
                    const location& /*loc*/,
                    bool,
                    bool,
                    module_init_extra& extra)
    {
      extra.set_module (new module_rcc ());

      return true;
    }

    //-
    // The `qt.rcc.config` module.
    //-
    bool
    rcc_config_init (scope& rs,
                     scope& /*bs*/,
                     const location& loc,
                     bool,
                     bool,
                     module_init_extra& extra)
    {
      // Load qt.rcc.guess and share its module instance as ours.
      //
      extra.module = load_module (rs, rs, "qt.rcc.guess", loc, extra.hints);

      return true;
    }

    //-
    // The `qt.rcc` module.
    //-
    bool
    rcc_init (scope& rs,
              scope& /*bs*/,
              const location& loc,
              bool,
              bool,
              module_init_extra& extra)
    {
      // Load qt.rcc.config and share its module instance as ours.
      //
      extra.module = load_module (rs, rs, "qt.rcc.config", loc, extra.hints);

      return true;
    }

    //-
    // The `qt.uic.guess` module.
    //-
    bool
    uic_guess_init (scope& /*rs*/,
                    scope& /*bs*/,
                    const location& /*loc*/,
                    bool,
                    bool,
                    module_init_extra& extra)
    {
      extra.set_module (new module_uic ());

      return true;
    }

    //-
    // The `qt.uic.config` module.
    //-
    bool
    uic_config_init (scope& rs,
                     scope& /*bs*/,
                     const location& loc,
                     bool,
                     bool,
                     module_init_extra& extra)
    {
      // Load qt.uic.guess and share its module instance as ours.
      //
      extra.module = load_module (rs, rs, "qt.uic.guess", loc, extra.hints);

      return true;
    }

    //-
    // The `qt.uic` module.
    //-
    bool
    uic_init (scope& rs,
              scope& /*bs*/,
              const location& loc,
              bool,
              bool,
              module_init_extra& extra)
    {
      // Load qt.uic.config and share its module instance as ours.
      //
      extra.module = load_module (rs, rs, "qt.uic.config", loc, extra.hints);

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
      {nullptr,         nullptr, nullptr}
    };

    const module_functions*
    build2_qt_load ()
    {
      return mod_functions;
    }
  }
}
