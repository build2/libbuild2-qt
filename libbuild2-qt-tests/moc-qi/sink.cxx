#include <QtCore/QVariant> // For QObject::setProperty() argument

#include "sink.hxx"

class Sink::number: public QObject
{
  Q_OBJECT

public:
  Q_PROPERTY (int value MEMBER val_)

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

#include "moc_sink.cxx"
#include "sink.moc"
