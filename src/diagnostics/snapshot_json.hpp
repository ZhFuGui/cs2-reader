#pragma once
#include "model/snapshot.hpp"
#include <string>
namespace cs2::diagnostics {
std::string snapshot_json(const model::Snapshot&);
}
