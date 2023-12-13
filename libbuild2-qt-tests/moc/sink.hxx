#pragma once

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
  // Undefined reference errors during link mean QtCore's macros weren't
  // passed to moc which is probably a bug in the module or ./buildfile.
  //
#ifdef QT_CORE_LIB
  void
  recv_num (int);
#endif

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
