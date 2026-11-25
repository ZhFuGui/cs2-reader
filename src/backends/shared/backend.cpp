#include "backends/shared/backend.hpp"
#include "memory/read_error.hpp"
#include <readers/memory.hpp>
#include <stdexcept>
#include <string>

namespace cs2::backends {
namespace {
namespace shared = readers::memory;

[[noreturn]] void unavailable(shared::Error error) {
    if (error.code == shared::ErrorCode::InvalidArgument)
        throw std::invalid_argument("Invalid shared memory request");
    throw memory::ReadUnavailable("Shared memory source is unavailable");
}
memory::ProcessState process_state(shared::SessionState state) noexcept {
    using State = memory::ProcessState;
    switch (state) {
    case shared::SessionState::Current:
        return State::Current;
    case shared::SessionState::Unavailable:
        return State::Unreadable;
    case shared::SessionState::Exited:
        return State::Exited;
    case shared::SessionState::Replaced:
        return State::Changed;
    case shared::SessionState::NeedsRebind:
        return State::AddressSpaceChanged;
    }
    return State::Unreadable;
}

class Channel final : public memory::IReadChannel {
  public:
    Channel(std::unique_ptr<shared::ReadChannel> source, shared::BackendKind backend)
        : source_(std::move(source)), hypermem_(backend == shared::BackendKind::HyperMem) {
        if (!source_)
            throw std::logic_error("Shared memory did not provide a channel");
    }
    memory::TransferResult read(memory::Address address, void* output, std::size_t bytes) noexcept override {
        return source_->read(address, output, bytes);
    }
    void read_many(std::span<memory::ReadRequest> requests) noexcept override { source_->read_many(requests); }
    void read_dependent(std::span<memory::DependentRead> requests) noexcept override {
        source_->read_dependent(requests);
    }
    diagnostics::ReadMetrics take_backend_metrics() noexcept override {
        const auto counters = source_->take_metrics();
        diagnostics::ReadMetrics result;
        if (hypermem_) {
            result.hypercall_single = counters.single_calls;
            result.hypercall_batch = counters.batch_calls + counters.dependent_calls;
            result.batch_items = counters.batch_items + counters.dependent_items;
            result.hypercall_dependent = counters.dependent_calls;
            result.dependent_items = counters.dependent_items;
            result.batch_errors = counters.backend_failures;
            result.batch_item_splits = counters.item_splits;
            result.batch_byte_splits = counters.byte_splits;
        }
        return result;
    }

  private:
    std::unique_ptr<shared::ReadChannel> source_;
    const bool hypermem_;
};

class Session final : public memory::ISession {
  public:
    Session(std::unique_ptr<shared::Session> source, shared::BackendKind backend)
        : source_(std::move(source)), backend_(backend) {
        if (!source_)
            throw std::logic_error("Shared memory did not provide a session");
    }
    memory::ProcessIdentity identity() const noexcept override { return source_->identity(); }
    std::uint64_t binding_id() const noexcept override { return source_->binding_id(); }
    memory::ProcessState state() const override { return process_state(source_->check()); }
    std::unique_ptr<memory::ISession> rebind() const override {
        auto rebound = source_->try_rebind();
        if (!rebound)
            unavailable(rebound.error());
        auto replacement = std::move(rebound).value();
        if (!replacement || replacement->identity() != identity())
            throw memory::ReadUnavailable("Shared memory replacement identity changed");
        return std::make_unique<Session>(std::move(replacement), backend_);
    }
    std::vector<memory::Module> modules(std::span<const std::u16string_view> names) const override {
        auto modules = source_->modules(names);
        if (!modules)
            unavailable(modules.error());
        return std::move(modules).value();
    }
    std::unique_ptr<memory::IReadChannel> open_reader() const override {
        try {
            return std::make_unique<Channel>(source_->open_reader(), backend_);
        } catch (const std::bad_alloc&) {
            throw memory::ReadUnavailable("Shared memory channel allocation is unavailable");
        }
    }

  private:
    const std::unique_ptr<shared::Session> source_;
    const shared::BackendKind backend_;
};

class Backend final : public memory::IBackend, public memory::IProcessCatalog {
  public:
    Backend(memory::ProcessNameKey name, std::string_view image_name)
        : source_(shared::create()), name_(name), image_name_(image_name) {
#if defined(CS2_READER_R3)
        constexpr auto expected = shared::BackendKind::R3;
#else
        constexpr auto expected = shared::BackendKind::HyperMem;
#endif
        if (!source_ || source_->backend() != expected)
            throw std::logic_error("Shared memory backend does not match the product build");
    }
    memory::IProcessCatalog& process_catalog() noexcept override { return *this; }
    std::optional<memory::ProcessIdentity> find(memory::ProcessNameKey name,
                                                std::optional<std::uint32_t> selected) override {
        if (name != name_)
            throw std::invalid_argument("Invalid shared memory target name");
        if (!pending_ || selected != selected_) {
            auto opened = source_->try_open({image_name_, selected});
            if (!opened) {
                if (absent(opened.error())) {
                    pending_.reset();
                    return {};
                }
                unavailable(opened.error());
            }
            auto candidate = std::move(opened).value();
            if (!candidate || !candidate->identity().pid || !candidate->identity().creation_time ||
                (selected && candidate->identity().pid != *selected))
                throw memory::ReadUnavailable("Shared memory discovery identity is unavailable");
            pending_ = std::move(candidate);
            selected_ = selected;
        }
        return pending_->identity();
    }
    void invalidate() override { pending_.reset(); }
    std::unique_ptr<memory::ISession> attach(memory::ProcessIdentity process, memory::AccessMode access) override {
        if (access != memory::AccessMode::ReadOnly)
            throw std::invalid_argument("Shared memory only supports read access");
        if (!pending_ || pending_->identity() != process) {
            auto opened = source_->try_open({image_name_, process.pid});
            if (!opened)
                unavailable(opened.error());
            auto candidate = std::move(opened).value();
            if (!candidate || candidate->identity() != process)
                throw memory::ReadUnavailable("Shared memory binding identity changed");
            pending_ = std::move(candidate);
            selected_ = process.pid;
        }
        auto state = pending_->check();
        if (state == shared::SessionState::NeedsRebind) {
            auto rebound = pending_->try_rebind();
            if (!rebound)
                unavailable(rebound.error());
            auto candidate = std::move(rebound).value();
            if (!candidate || candidate->identity() != process)
                throw memory::ReadUnavailable("Shared memory replacement identity changed");
            pending_ = std::move(candidate);
            state = pending_->check();
        }
        if (state != shared::SessionState::Current)
            throw memory::ReadUnavailable("Shared memory binding is not current");
        // 发现结果直接交接，不为同一个候选再次打开会话。
        return std::make_unique<Session>(std::move(pending_), source_->backend());
    }
    std::optional<memory::ProcessIdentity> find_process_identity(std::uint32_t pid) override {
        auto opened = source_->try_open({image_name_, pid});
        if (!opened) {
            if (absent(opened.error()))
                return {};
            unavailable(opened.error());
        }
        const auto& session = opened.value();
        if (!session || session->identity().pid != pid)
            throw memory::ReadUnavailable("Shared memory identity check returned a different process");
        return session->identity();
    }

  private:
    static bool absent(shared::Error error) noexcept {
        return error.code == shared::ErrorCode::NotFound || error.code == shared::ErrorCode::ProcessExited ||
               error.code == shared::ErrorCode::ProcessReplaced;
    }
    std::unique_ptr<shared::Memory> source_;
    const memory::ProcessNameKey name_;
    const std::string image_name_;
    std::unique_ptr<shared::Session> pending_;
    std::optional<std::uint32_t> selected_;
};
} // namespace
std::unique_ptr<memory::IBackend> make_shared_backend(memory::ProcessNameKey name, std::string_view image_name) {
    return std::make_unique<Backend>(name, image_name);
}
} // namespace cs2::backends
