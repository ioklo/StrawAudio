#include "strawaudio/UdpSender.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace straw {
UdpSender::UdpSender(std::string_view host, std::string_view port, size_t packetBytes, Counters& counters)
    : counters{counters}
{
    if (packetBytes < 64 || packetBytes > 1400 || packetBytes % 4)
        throw std::invalid_argument("UDP packet size must be 64..1400 bytes and a multiple of 4");

    packet.resize(packetBytes);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* addresses{};
    std::string ownedHost{host};
    std::string ownedPort{port};
    auto result = getaddrinfo(ownedHost.c_str(), ownedPort.c_str(), &hints, &addresses);

    if (result)
        throw std::runtime_error(std::string{"Resolve destination: "} + gai_strerror(result));

    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner{addresses, freeaddrinfo};
    int lastError{};

    for (auto* address = addresses; address; address = address->ai_next)
    {
        auto candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);

        if (candidate < 0)
        {
            lastError = errno;
            continue;
        }

        auto flags = fcntl(candidate, F_GETFL, 0);
        auto nonblocking = fcntl(candidate, F_SETFL, flags | O_NONBLOCK);
        auto closeOnExec = fcntl(candidate, F_SETFD, FD_CLOEXEC);
        auto connected = connect(candidate, address->ai_addr, address->ai_addrlen);

        if (flags >= 0 && nonblocking == 0 && closeOnExec == 0 && connected == 0)
        {
            // 커널 송신 큐도 작게 유지한다. 실제 크기는 OS가 조정할 수 있다.
            int sendBuffer{8192};
            setsockopt(candidate, SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer));
            socket = candidate;
            return;
        }

        lastError = errno;
        close(candidate);
    }

    throw std::runtime_error(std::string{"Open UDP destination: "} + std::strerror(lastError));
}

UdpSender::~UdpSender()
{
    if (socket >= 0)
        close(socket);
}

void UdpSender::Append(std::span<char> bytes)
{
    while (!bytes.empty())
    {
        auto count = std::min(bytes.size(), packet.size() - used);
        std::memcpy(packet.data() + used, bytes.data(), count);
        used += count;
        bytes = bytes.subspan(count);

        if (used == packet.size())
        {
            SendPacket();
            used = 0;
        }
    }
}

void UdpSender::SendPacket()
{
    auto sent = send(socket, packet.data(), packet.size(), 0);

    if (sent == static_cast<ssize_t>(packet.size()))
    {
        counters.sentBytes.fetch_add(packet.size(), std::memory_order_relaxed);
        counters.sentPackets.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    counters.droppedPackets.fetch_add(1, std::memory_order_relaxed);

    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS && errno != EINTR)
        counters.sendErrors.fetch_add(1, std::memory_order_relaxed);
}

void UdpSender::DiscardPartial()
{
    // 종료·적체 해소 때 오래된 꼬리를 다음 오디오에 붙이지 않는다.
    used = 0;
}
} // namespace straw
