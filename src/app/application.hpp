#pragma once
#include "app/options.hpp"
#include <iosfwd>
namespace cs2::app {
int run(const Options&, std::ostream& log);
}
