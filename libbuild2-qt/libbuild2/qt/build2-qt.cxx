#include <qt/build2-qt.hxx>

#include <ostream>
#include <stdexcept>

using namespace std;

namespace build2_qt
{
  void say_hello (ostream& o, const string& n)
  {
    if (n.empty ())
      throw invalid_argument ("empty name");

    o << "Hello, " << n << '!' << endl;
  }
}
