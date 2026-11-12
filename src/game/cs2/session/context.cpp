#include "game/cs2/session/context.hpp"
#include "diagnostics/obf.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include <algorithm>
#include <stdexcept>
namespace cs2::game {
namespace {
bool equals(std::u16string_view a, std::u16string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto fold = [](char16_t c) { return c >= u'A' && c <= u'Z' ? c + 32 : c; };
        if (fold(a[i]) != fold(b[i]))
            return false;
    }
    return true;
}
} // namespace
Context open_context(memory::IReadChannel& channel, const memory::ISession& session) {
    Context context;
    const auto client_name = WOBF_U16(L"client.dll");
    const auto engine_name = WOBF_U16(L"engine2.dll");
    for (auto& module : memory::read_modules(channel, session.peb())) {
        if (equals(module.name, client_name))
            context.client = std::move(module);
        else if (equals(module.name, engine_name))
            context.engine = std::move(module);
    }
    if (!context.client.base || !context.engine.base)
        throw ContextPending(OBF("Waiting for the required game modules to load"));
    // 通用枚举允许 PE32 资源映像；只有实际使用 CS2 偏移的代码模块必须为 x64。
    if (!context.client.is_x64() || !context.engine.is_x64())
        throw std::runtime_error("Required game modules must be x64 PE images");
    if (context.client.size <= offsets::client::dwEntityList + sizeof(memory::Address) ||
        context.engine.size <= offsets::engine::dwBuildNumber + sizeof(std::uint32_t))
        throw std::runtime_error("Offset profile does not fit the target modules");
    const auto build = memory::read_value<std::uint32_t>(channel, context.engine.base + offsets::engine::dwBuildNumber);
    if (!build || !*build)
        throw ContextPending("Target game build is unreadable or not initialized");
    if (*build != offsets::expected_build()) {
        // 只在不匹配路径再确认一次，避免把初始化/卸载中的变化当成已确认的新版本。
        const auto confirmed =
            memory::read_value<std::uint32_t>(channel, context.engine.base + offsets::engine::dwBuildNumber);
        if (confirmed != build)
            throw ContextPending("Target game build changed during verification");
        throw std::runtime_error("Offset build probe mismatch: observed=" + std::to_string(*build) + " expected=" +
                                 std::to_string(offsets::expected_build()) + "; update output before sampling");
    }
    context.build = *build;
    return context;
}
} // namespace cs2::game
