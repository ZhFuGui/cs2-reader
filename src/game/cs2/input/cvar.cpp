#include "game/cs2/input/cvar.hpp"
#include "memory/read_plan.hpp"
#include <array>
#include <algorithm>
#include <cmath>
namespace cs2::game {
std::optional<float> CvarBatch::Sample::real() const {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return readable && type == 7 && std::isfinite(value) ? std::optional(value) : std::nullopt;
}
std::optional<std::uint8_t> CvarBatch::Sample::boolean() const {
    return readable && type == 0 ? std::optional(static_cast<std::uint8_t>(bits)) : std::nullopt;
}
CvarBatch::CvarBatch(memory::IReadChannel& reader, const Context& context, std::span<const CvarRequest> requests)
    : requests_(requests.begin(), requests.end()), samples_(requests.size()), names_(requests.size()) {
    struct Tickets {
        memory::ReadPlan::Ticket data, type, flags, value;
        std::optional<memory::ReadPlan::Ticket> name, text;
    };
    std::vector<Tickets> tickets(requests.size());
    memory::ReadPlan plan(requests.size() * 6);
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        auto& sample = samples_[i];
        auto& t = tickets[i];
        sample.root = context.client.base + request.root + 8;
        t.data = plan.pointer(sample.root);
        t.type = plan.add_from(t.data, 0x28, sample.type);
        t.flags = plan.add_from(t.data, 0x30, sample.flags);
        t.value = plan.add_from(t.data, 0x58, &sample.bits, request.real ? sizeof(float) : sizeof(std::uint8_t));
        if (!request.name.empty()) {
            t.name = plan.pointer_from(t.data, 0);
            names_[i].resize(request.name.size() + 1);
            t.text = plan.add_from(*t.name, 0, names_[i].data(), names_[i].size());
        }
    }
    plan.execute(reader);
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto& sample = samples_[i];
        const auto& t = tickets[i];
        const auto data = plan.pointer_value(t.data);
        if (!data || !memory::user_range(*data) || !plan.complete(t.type) || !plan.complete(t.flags) ||
            !plan.complete(t.value))
            continue;
        sample.data = *data;
        if (t.name) {
            const auto name = plan.pointer_value(*t.name);
            if (!name || !plan.complete(*t.text) || names_[i].back() != '\0' ||
                std::string_view(names_[i].data(), names_[i].size() - 1) != requests[i].name)
                continue;
            sample.name = *name;
        }
        sample.readable = true;
    }
}
std::vector<std::uint8_t> CvarBatch::close(memory::IReadChannel& reader) const {
    std::vector<Sample> values(samples_.size());
    std::vector<std::string> names(samples_.size());
    std::vector<std::vector<memory::ReadPlan::Ticket>> tickets(samples_.size());
    memory::ReadPlan plan(samples_.size() * 6);
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const auto& sample = samples_[i];
        if (!sample.readable)
            continue;
        auto& value = values[i];
        auto& t = tickets[i];
        t.push_back(plan.add(sample.root, value.data));
        t.push_back(plan.add(sample.data + 0x28, value.type));
        t.push_back(plan.add(sample.data + 0x30, value.flags));
        t.push_back(plan.add(sample.data + 0x58, &value.bits, requests_[i].real ? sizeof(float) : sizeof(std::uint8_t)));
        if (!requests_[i].name.empty()) {
            names[i].resize(names_[i].size());
            t.push_back(plan.add(sample.data, value.name));
            t.push_back(plan.add(sample.name, names[i].data(), names[i].size()));
        }
    }
    plan.execute(reader);
    std::vector<std::uint8_t> result(samples_.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto& a = samples_[i];
        const auto& b = values[i];
        result[i] = a.readable && std::all_of(tickets[i].begin(), tickets[i].end(),
                                             [&](auto ticket) { return plan.complete(ticket); }) &&
                    a.data == b.data && a.type == b.type && a.flags == b.flags && a.bits == b.bits &&
                    (requests_[i].name.empty() || (a.name == b.name && names_[i] == names[i]));
    }
    return result;
}
} // namespace cs2::game
