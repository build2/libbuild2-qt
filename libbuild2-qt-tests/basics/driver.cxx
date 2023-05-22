#include <QtCore/QFile>
#include <QtCore/QResource>

#include "qrc_foo.hxx"

int
main ()
{
  QFile foo (":/foo.txt");
  assert (foo.exists ());

  QFile bar (":/bar.txt");
  assert (bar.exists ());

  assert (QResource::registerResource (OUT_BASE"baz.rcc"));
  QFile baz (":/baz.txt");
  assert (baz.exists ());
}
