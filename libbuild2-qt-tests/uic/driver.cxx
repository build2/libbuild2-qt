#include "ui_foo.hxx"

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
