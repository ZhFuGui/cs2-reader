#pragma once
#include "capture/source.hpp"
#include "runtime/binding.hpp"
#include "runtime/control.hpp"
#include "runtime/output.hpp"

namespace cs2::runtime {
struct Limits {
    std::optional<double> wait_seconds, duration_seconds;
    std::uint64_t samples = 0;
};
struct Environment {
    memory::IBackend& backend;
    memory::IProcessCatalog& catalog;
    const capture::ISourceFactory& factory;
    const capture::IClock& clock;
    capture::FrequencyControl& frequency;
    IRunControl& control;
    IOutput* output = nullptr;
};
// 主线程内运行，不增加管理线程；游戏细节、平台停止来源与发布实现均由外部组合。
int run(const Target&, const Limits&, const Environment&, std::ostream&);
} // namespace cs2::runtime
