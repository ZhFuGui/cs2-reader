#include "game/cs2/capture/factory.hpp"
#include "game/cs2/capture/collector.hpp"
#include <stdexcept>

namespace cs2::game {
std::unique_ptr<capture::ISource> SourceFactory::open(const memory::ISession& session,
                                                      const capture::IClock& clock) const {
    auto bootstrap = session.open_reader();
    if (!bootstrap)
        throw std::logic_error("Backend did not provide a bootstrap reader");
    auto context = open_context(*bootstrap, session);
    return std::make_unique<Collector>(session, std::move(context), clock, workers_, bones_);
}
} // namespace cs2::game
