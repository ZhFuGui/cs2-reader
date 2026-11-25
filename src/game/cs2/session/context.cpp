#include "game/cs2/session/context.hpp"
#include "capture/source.hpp"
#include "diagnostics/obf.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include <array>
#include <stdexcept>
namespace cs2::game {
Context open_context(memory::IReadChannel& channel, const memory::ISession& session) {
    Context context;
    const auto client_name = WOBF_U16(L"client.dll");
    const auto engine_name = WOBF_U16(L"engine2.dll");
    const std::array<std::u16string_view, 2> names{client_name, engine_name};
    for (auto& module : session.modules(names)) {
        if (memory::same_module_name(module.name, client_name))
            context.client = std::move(module);
        else if (memory::same_module_name(module.name, engine_name))
            context.engine = std::move(module);
    }
    if (!context.client.base || !context.engine.base)
        throw ContextPending(OBF("Waiting for the required game modules to load"));
    // 通用枚举允许 PE32 资源映像；只有实际使用 CS2 偏移的代码模块必须为 x64。
    if (!context.client.is_x64() || !context.engine.is_x64())
        throw capture::IncompatibleSource{};
    if (context.client.size <= offsets::client::dwEntityList + sizeof(memory::Address) ||
        context.engine.size <= offsets::engine::dwBuildNumber + sizeof(std::uint32_t))
        throw capture::IncompatibleSource{};
    const auto build = memory::read_value<std::uint32_t>(channel, context.engine.base + offsets::engine::dwBuildNumber);
    if (!build || !*build)
        throw ContextPending("Target game build is unreadable or not initialized");
    if (*build != offsets::expected_build()) {
        // 只在不匹配路径再确认一次，避免把初始化/卸载中的变化当成已确认的新版本。
        const auto confirmed =
            memory::read_value<std::uint32_t>(channel, context.engine.base + offsets::engine::dwBuildNumber);
        if (confirmed != build)
            throw ContextPending("Target game build changed during verification");
        throw capture::IncompatibleSource{};
    }
    context.build = *build;
    return context;
}
} // namespace cs2::game
