#include <QtCore/QFile>
#include <QtCore/QResource>

#include <rcc/qrc_foo.hxx>

int
main ()
{
  QFile foo (":/foo.txt");
  assert (foo.exists ());
  QFile foo2 (":/foo2.txt");
  assert (foo.exists ());
  QFile foo3 (":/foo3.txt");
  assert (foo.exists ());

  QFile bar (":/bar.txt");
  assert (bar.exists ());

  assert (QResource::registerResource (OUT_BASE"baz.rcc"));
  {
    QFile baz (":/baz.txt");
    assert (baz.exists ());
  }
  assert (QResource::unregisterResource (OUT_BASE"baz.rcc"));
}
