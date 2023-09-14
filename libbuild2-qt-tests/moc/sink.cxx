#include <QtCore/QVariant> // For QObject::setProperty() argument

#include <moc/sink.hxx>

class Sink::number: public QObject
{
  Q_OBJECT

public:
  // Undefined reference errors during link mean QtCore's macros weren't
  // passed to moc which is probably a bug in the module or ./buildfile.
  //
#ifdef QT_CORE_LIB
  Q_PROPERTY (int value MEMBER val_)
#endif

private:
  int val_ = 0;
};

Sink::Sink () : num_ (new number) {}

Sink::~Sink () = default;

void Sink::
recv_num (int n)
{
  num_->setProperty ("value", n);
}

int Sink::
num () const
{
  return num_->property ("value").value<int> ();
}

#include <moc/sink_moc.cxx>
#include <moc/sink.moc>
