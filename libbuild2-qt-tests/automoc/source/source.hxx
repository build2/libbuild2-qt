#pragma once

#include <QtCore/QObject>

class Source: public QObject
{
  Q_OBJECT

signals:
  // Undefined reference errors during link mean QtCore's macros weren't
  // passed to moc which is probably a bug in the module or ./buildfile.
  //
#ifdef QT_CORE_LIB
  void
  send_num (int);
#endif
};
