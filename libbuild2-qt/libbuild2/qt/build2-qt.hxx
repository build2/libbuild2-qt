#pragma once

#include <iosfwd>
#include <string>

#include <qt/export.hxx>

namespace build2_qt
{
  // Print a greeting for the specified name into the specified
  // stream. Throw std::invalid_argument if the name is empty.
  //
  LIBBUILD2_QT_SYMEXPORT void
  say_hello (std::ostream&, const std::string& name);
}
