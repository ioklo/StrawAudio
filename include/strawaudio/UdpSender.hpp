#pragma once
#include "Audio.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace straw {
class UdpSender
{
public:
    UdpSender(std::string_view host, std::string_view port, size_t packetBytes, Counters& counters);
    ~UdpSender();
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;
    void Append(std::span<char> bytes);
    void DiscardPartial();

private:
    void SendPacket();
    int socket = -1;
    Counters& counters;
    std::vector<char> packet;
    size_t used = 0;
};
} // namespace straw
