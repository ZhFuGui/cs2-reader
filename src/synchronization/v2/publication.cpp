#include "synchronization/v2/publication.hpp"
#include "model/snapshot.hpp"

namespace cs2::synchronization::v2 {
bool Publication::submit(std::shared_ptr<const model::Snapshot> snapshot) {
    if (!snapshot)
        return false;
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return false;
        if (state_.status.last_success && snapshot->sequence <= *state_.status.last_success)
            throw std::runtime_error("Capture sequence must increase");
        const bool new_epoch = state_.status.epoch && *state_.status.epoch != snapshot->epoch;
        if (new_epoch || state_.status.availability != model::Availability::Ready) {
            ++state_.token;
            ++state_.status_revision;
            if (new_epoch)
                ++state_.invalidation_serial;
            state_.packet.reset();
        }
        state_.status = {model::Availability::Ready, snapshot->epoch, snapshot->sequence};
        if (input_)
            ++coalesced_;
        input_ = EncodingTask{std::move(snapshot), state_.token};
    }
    ready_.notify_one();
    return true;
}
bool Publication::invalidate(model::Availability availability, model::ReasonCode reason,
                             std::optional<std::uint64_t> epoch) {
    std::lock_guard lock(mutex_);
    if (stopping_ ||
        (state_.status.availability == availability && state_.status.reason == reason && state_.status.epoch == epoch))
        return false;
    ++state_.token;
    ++state_.status_revision;
    ++state_.invalidation_serial;
    state_.status.availability = availability;
    state_.status.reason = reason;
    state_.status.epoch = epoch;
    state_.packet.reset();
    input_.reset();
    return true;
}
std::optional<EncodingTask> Publication::take(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const auto ready = [&] { return stopping_ || input_.has_value(); };
    if (timeout == std::chrono::milliseconds::max())
        ready_.wait(lock, ready);
    else
        ready_.wait_for(lock, timeout, ready);
    if (stopping_ || !input_)
        return {};
    auto task = std::move(input_);
    input_.reset();
    return task;
}
bool Publication::commit(std::shared_ptr<const EncodedSnapshot> packet) {
    if (!packet)
        throw std::invalid_argument("Cannot commit an empty encoded snapshot");
    std::lock_guard lock(mutex_);
    if (stopping_ || state_.token != packet->token || state_.status.availability != model::Availability::Ready)
        return false;
    state_.packet = std::move(packet);
    encoding_fault_.reset();
    ++encoded_;
    return true;
}
bool Publication::encoding_failed(std::uint64_t token) {
    std::lock_guard lock(mutex_);
    if (stopping_ || state_.token != token)
        return false; // 晚到的旧代次异常也不能覆盖当前健康诊断。
    encoding_fault_ = model::Fault{model::ReasonCode::EncodingFailed};
    ++state_.token;
    ++state_.status_revision;
    ++state_.invalidation_serial;
    state_.status.availability = model::Availability::Unavailable;
    state_.status.reason = model::ReasonCode::EncodingFailed;
    state_.packet.reset();
    input_.reset();
    return true;
}
void Publication::network_failed(std::optional<std::uint32_t> os_code) {
    std::lock_guard lock(mutex_);
    network_fault_ = model::Fault{model::ReasonCode::NetworkUnavailable, os_code};
}
void Publication::network_recovered() {
    std::lock_guard lock(mutex_);
    network_fault_.reset();
}
void Publication::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return;
        stopping_ = true;
        ++state_.token;
        state_.status.availability = model::Availability::Stopping;
        state_.status.reason = model::ReasonCode::None;
        input_.reset();
        state_.packet.reset();
    }
    ready_.notify_all();
}
PublicationView Publication::view() const {
    std::lock_guard lock(mutex_);
    return state_;
}
PublicationStats Publication::stats() const {
    std::lock_guard lock(mutex_);
    return {encoded_, coalesced_, encoding_fault_, network_fault_};
}
} // namespace cs2::synchronization::v2
