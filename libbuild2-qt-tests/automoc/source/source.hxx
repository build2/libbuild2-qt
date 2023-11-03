#pragma once

#include <QtCore/QObject>

class Source: public QObject
{
  // Add things around the Q_OBJECT macro to test the automoc rule for false
  // negatives. (See sink.hxx for the conventional case and nomoc.hxx.in for
  // the false positive tests.)
  //

  /* */Q_OBJECT;/**/ //

signals:
  // Undefined reference errors during link mean QtCore's macros weren't
  // passed to moc which is probably a bug in the module or ./buildfile.
  //
#ifdef QT_CORE_LIB
  void
  send_num (int);
#endif
};
