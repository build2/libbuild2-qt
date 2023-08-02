#include <QtCore/QObject>

#include <memory> // unique_ptr

class Sink: public QObject
{
  Q_OBJECT

public:
  Sink ();
  ~Sink ();

public slots:
  // Receive the number.
  //
  void
  recv_num (int);

public:
  // Return the stored number.
  //
  int
  num () const;

private:
  // Number storage implemented using the Qt Property System (thus requiring
  // the source file to be moc'd).
  //
  class number;
  std::unique_ptr<number> num_;
};
