#pragma once
#include "memory/backend.hpp"
#include "memory/process_catalog.hpp"
#include "memory/read_error.hpp"

namespace cs2::runtime {
struct Target {
    memory::ProcessNameKey name;
    std::optional<std::uint32_t> pid;
    memory::AccessMode access = memory::AccessMode::ReadOnly;
};
class BindingPending final : public memory::ReadUnavailable {
  public:
    enum class Stage { Discovery, Attach, SelectedGone };
    BindingPending(Stage value, const char* message) : memory::ReadUnavailable(message), stage(value) {}
    const Stage stage;
};
// 进程身份与会话的唯一所有者；不会创建采集 worker，也不决定重试节奏。
class Binding final {
  public:
    Binding(memory::IBackend&, memory::IProcessCatalog&, const Target&);
    memory::ISession* acquire();
    memory::ProcessState observe();
    memory::ProcessState discard_if_gone();
    // 先收尾借用旧会话的采集器。新绑定成功前保留旧身份作为核验锚点，不重新枚举。
    void refresh_address_space();
    void release(); // 调用前必须先关闭引用该会话的采集器。

  private:
    memory::IBackend& backend_;
    memory::IProcessCatalog& catalog_;
    const Target target_;
    std::unique_ptr<memory::ISession> session_;
    bool refresh_pending_ = false;
    unsigned unreadable_checks_ = 0;
};
} // namespace cs2::runtime
