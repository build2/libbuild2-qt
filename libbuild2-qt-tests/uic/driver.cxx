#include "ui_foo.hxx"

int
main (int argc, char** argv)
{
  // If true, show the UI.
  //
  bool show (argc < 2 || QString (argv[1]) != "--no-show-ui");

  QApplication app (argc, argv);

  QWidget widget;
  Ui::Foo ui;
  ui.setupUi (&widget);
  widget.show ();

  return show ? app.exec () : 0;
}
