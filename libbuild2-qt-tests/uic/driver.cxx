#include "ui_foo.hxx"

// @@ Show unless option.

int
main (int argc, char** argv)
{
  QApplication app (argc, argv);

  QWidget widget;
  Ui::Foo ui;
  ui.setupUi (&widget);
  widget.show ();

  return 0;
}
