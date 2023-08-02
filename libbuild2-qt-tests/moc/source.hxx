#include <QtCore/QObject>

class Source: public QObject
{
  Q_OBJECT

signals:
  void
  send_num (int);
};
