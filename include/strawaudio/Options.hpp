#pragma once
#include <cstddef>
#include <string>

namespace straw {
struct Options
{
    std::string host = "127.0.0.1";
    std::string port = "5000";
    size_t packetBytes = 1024;
    unsigned bufferMs = 100;
    unsigned statsSeconds = 5;
    double duration = 0;
    double toneHz = 0;
    bool mute = true;
    bool help = false;
    bool version = false;
};

Options ParseOptions(int argc, char* const argv[]);
const char* Usage();
} // namespace straw
