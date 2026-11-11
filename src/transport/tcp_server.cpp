#include "transport/tcp_server.hpp"
#include "transport/ipv4.hpp"
#include "cs2_reader/v1/record.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace cs2::transport {
namespace {
[[noreturn]] void socket_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed, WSA=" + std::to_string(WSAGetLastError()));
}
struct Socket {
    SOCKET value = INVALID_SOCKET;
    Socket() = default;
    explicit Socket(SOCKET v) : value(v) {}
    ~Socket() {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : value(std::exchange(other.value, INVALID_SOCKET)) {}
};
struct Event {
    WSAEVENT value = WSACreateEvent();
    Event() {
        if (value == WSA_INVALID_EVENT)
            socket_error("WSACreateEvent");
    }
    ~Event() { WSACloseEvent(value); }
};
struct Startup {
    Startup() {
        WSADATA data{};
        if (const auto error = WSAStartup(MAKEWORD(2, 2), &data); error)
            throw std::runtime_error("WSAStartup failed");
    }
    ~Startup() { WSACleanup(); }
};
struct Peer {
    ConnectionId id;
    Socket socket;
    Event event;
    cs2_reader::v1::RecordDecoder decoder;
    std::optional<Record> output;
    std::size_t sent_bytes{};
    bool writable = true, stamped = false;
    Peer(ConnectionId identity, Socket value) : id(identity), socket(std::move(value)) {
        const BOOL yes = TRUE;
        if (setsockopt(socket.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)) ==
                SOCKET_ERROR ||
            WSAEventSelect(socket.value, event.value, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
            socket_error("configure peer");
    }
};
void stamp(Record& record, std::uint64_t value) {
    for (auto offset : record.stamp_offsets) {
        if (offset > record.bytes.size() || record.bytes.size() - offset < 8)
            throw std::runtime_error("invalid record stamp");
        for (std::size_t i = 0; i < 8; ++i)
            record.bytes[offset + i] = std::byte((value >> (8 * i)) & 0xffU);
    }
}
} // namespace
struct TcpServer::Impl {
    Startup startup;
    Socket listener;
    Event event;
    std::size_t maximum;
    ConnectionId next_id{};
    std::vector<std::unique_ptr<Peer>> peers;
    Impl(std::uint16_t port, std::size_t count, std::string_view bind_address) : maximum(count) {
        const auto ipv4 = parse_ipv4(bind_address);
        if (!ipv4)
            throw std::runtime_error("Invalid numeric IPv4 bind address");
        if (!port || !count || count > WSA_MAXIMUM_WAIT_EVENTS - 2)
            throw std::runtime_error("invalid TCP service limits");
        listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener.value == INVALID_SOCKET)
            socket_error("socket");
        const BOOL yes = TRUE;
        if (setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes),
                       sizeof(yes)) == SOCKET_ERROR)
            socket_error("exclusive socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(*ipv4);
        if (bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
            socket_error("bind");
        if (listen(listener.value, SOMAXCONN) == SOCKET_ERROR)
            socket_error("listen");
        if (WSAEventSelect(listener.value, event.value, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR)
            socket_error("listener event");
    }
};
TcpServer::TcpServer(std::uint16_t port, std::size_t count, std::string_view bind_address)
    : impl_(std::make_unique<Impl>(port, count, bind_address)) {}
TcpServer::~TcpServer() = default;
void TcpServer::run(IRecordHandler& handler, const capture::IClock& clock, const std::atomic_bool& stopping,
                    void* wake_event) {
    auto& state = *impl_;
    struct DisconnectAll {
        Impl& state;
        IRecordHandler& handler;
        ~DisconnectAll() {
            for (const auto& peer : state.peers)
                handler.disconnected(peer->id);
            state.peers.clear();
        }
    } cleanup{state, handler};
    bool more_work = false;
    while (!stopping.load()) {
        std::vector<WSAEVENT> events{static_cast<HANDLE>(wake_event), state.event.value};
        for (const auto& peer : state.peers)
            events.push_back(peer->event.value);
        const auto wait = WSAWaitForMultipleEvents(static_cast<DWORD>(events.size()), events.data(), FALSE,
                                                   more_work ? 0 : 10, FALSE);
        if (wait == WSA_WAIT_FAILED)
            socket_error("WSAWaitForMultipleEvents");
        more_work = false;
        if (stopping.load())
            break;
        if (WaitForSingleObject(state.event.value, 0) == WAIT_OBJECT_0) {
            WSANETWORKEVENTS notification{};
            if (WSAEnumNetworkEvents(state.listener.value, state.event.value, &notification) == SOCKET_ERROR)
                socket_error("listener events");
            if (notification.lNetworkEvents & FD_CLOSE)
                throw std::runtime_error("listener closed");
            for (std::size_t accepted = 0; accepted < 8; ++accepted) {
                const auto socket_value = accept(state.listener.value, nullptr, nullptr);
                if (socket_value == INVALID_SOCKET) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
                        break;
                    socket_error("accept");
                }
                if (state.peers.size() >= state.maximum) {
                    closesocket(socket_value);
                    continue;
                }
                Socket accepted_socket(socket_value);
                // socket在Peer构造成功前就交由RAII持有；异常只影响该连接。
                try {
                    auto peer = std::make_unique<Peer>(++state.next_id, std::move(accepted_socket));
                    handler.connected(peer->id);
                    state.peers.push_back(std::move(peer));
                } catch (...) {
                    handler.disconnected(state.next_id);
                }
            }
        }
        for (auto iterator = state.peers.begin(); iterator != state.peers.end();) {
            auto& peer = **iterator;
            bool keep = true;
            try {
                handler.poll(peer.id);
                WSANETWORKEVENTS notification{};
                if (WaitForSingleObject(peer.event.value, 0) == WAIT_OBJECT_0) {
                    if (WSAEnumNetworkEvents(peer.socket.value, peer.event.value, &notification) == SOCKET_ERROR)
                        socket_error("peer events");
                    if (notification.lNetworkEvents & FD_WRITE)
                        peer.writable = true;
                    if (notification.lNetworkEvents & (FD_READ | FD_CLOSE)) {
                        std::array<std::byte, 16384> buffer{};
                        for (std::size_t budget = 0; budget < 65536;) {
                            const auto count = recv(peer.socket.value, reinterpret_cast<char*>(buffer.data()),
                                                    static_cast<int>(buffer.size()), 0);
                            if (!count) {
                                peer.decoder.finish();
                                keep = false;
                                break;
                            }
                            if (count == SOCKET_ERROR) {
                                if (WSAGetLastError() == WSAEWOULDBLOCK)
                                    break;
                                socket_error("recv");
                            }
                            budget += static_cast<std::size_t>(count);
                            peer.decoder.feed(std::span{buffer}.first(static_cast<std::size_t>(count)), [&](auto body) {
                                const auto received = clock.now();
                                handler.received(peer.id, body, received);
                            });
                            if (budget >= 65536)
                                more_work = true;
                        }
                    }
                }
                if (keep && peer.output && !handler.valid(*peer.output)) {
                    if (peer.sent_bytes)
                        keep = false; // 半条旧代次记录不能与失效消息拼接，直接断开。
                    else
                        peer.output.reset();
                }
                if (keep && !peer.output) {
                    peer.output = handler.next(peer.id);
                    peer.sent_bytes = 0;
                    peer.stamped = false;
                }
                if (keep && peer.output && peer.writable) {
                    if (!handler.valid(*peer.output)) {
                        peer.output.reset();
                        ++iterator;
                        continue;
                    }
                    if (!peer.stamped) {
                        stamp(*peer.output, clock.now());
                        peer.stamped = true;
                    }
                    const auto remaining = peer.output->bytes.size() - peer.sent_bytes;
                    const auto count = send(peer.socket.value,
                                            reinterpret_cast<const char*>(peer.output->bytes.data() + peer.sent_bytes),
                                            static_cast<int>(std::min<std::size_t>(remaining, 65536)), 0);
                    if (count == SOCKET_ERROR) {
                        if (WSAGetLastError() == WSAEWOULDBLOCK)
                            peer.writable = false;
                        else
                            socket_error("send");
                    } else if (!count)
                        keep = false;
                    else {
                        peer.sent_bytes += static_cast<std::size_t>(count);
                        if (peer.sent_bytes == peer.output->bytes.size()) {
                            handler.sent(peer.id, *peer.output);
                            peer.output.reset();
                        }
                        more_work = true;
                    }
                }
            } catch (...) {
                keep = false;
            }
            if (!keep) {
                handler.disconnected(peer.id);
                iterator = state.peers.erase(iterator);
            } else
                ++iterator;
        }
    }
}
} // namespace cs2::transport
