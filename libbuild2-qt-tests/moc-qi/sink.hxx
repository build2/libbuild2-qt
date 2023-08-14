#pragma once

#include <QtCore/QObject>

class Sink: public QObject
{
  Q_OBJECT

public:
  int num;

public slots:
  // Receive the number.
  //
  void
  recv_num (int n);
};
