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
    if (context.client.size <= offsets::client::dwEntityList + sizeof(memory::Address) ||
        context.engine.size <= offsets::engine::dwBuildNumber + sizeof(std::uint32_t))
        throw std::runtime_error("Offset profile does not fit the target modules");
    const auto build = memory::read_value<std::uint32_t>(channel, context.engine.base + offsets::engine::dwBuildNumber);
    if (!build)
        throw std::runtime_error("Cannot read target game build");
    if (*build != offsets::expected_build())
        throw std::runtime_error("Offset build probe mismatch: observed=" + std::to_string(*build) + " expected=" +
                                 std::to_string(offsets::expected_build()) + "; update output before sampling");
    context.build = *build;
    return context;
}
} // namespace cs2::game
