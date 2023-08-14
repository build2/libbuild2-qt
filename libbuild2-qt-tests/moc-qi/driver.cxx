#include "sink.hxx"
#include "source.hxx"

int
main ()
{
  // Send a number between two objects using the signals & slots mechanism.
  //

  Source source;
  Sink sink;

  QObject::connect (&source, &Source::send_num,
                    &sink, &Sink::recv_num);

  source.send_num (123);
  assert (sink.num == 123);

  source.send_num (456);
  assert (sink.num == 456);

  return 0;
}
