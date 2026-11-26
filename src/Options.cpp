#include "strawaudio/Options.hpp"
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <locale>
#include <string_view>

namespace straw {
namespace {
unsigned Integer(std::string_view value, unsigned minimum, unsigned maximum)
{
    unsigned number{};
    auto result = std::from_chars(value.data(), value.data() + value.size(), number);

    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || number < minimum || number > maximum)
        throw std::invalid_argument("Invalid integer: " + std::string{value});

    return number;
}

double Decimal(std::string_view value, double minimum, double maximum)
{
    double number{};
    // 부동소수점 from_chars는 macOS 26부터 제공되므로 CLI에서만 classic locale 스트림을 사용한다.
    std::istringstream input{std::string{value}};
    input.imbue(std::locale::classic());
    input >> std::noskipws >> number;

    if (input.fail() || input.peek() != std::char_traits<char>::eof() || !std::isfinite(number) || number < minimum ||
        number > maximum)
        throw std::invalid_argument("Invalid number: " + std::string{value});

    return number;
}

void Destination(std::string_view value, Options& options)
{
    auto separator = value.rfind(':');

    if (separator == std::string_view::npos || !separator || separator + 1 == value.size())
        throw std::invalid_argument("Destination must be host:port or [IPv6]:port");

    auto host = value.substr(0, separator);

    if (host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    else if (host.find(':') != std::string_view::npos)
        throw std::invalid_argument("IPv6 addresses require brackets: [::1]:5000");

    if (host.empty())
        throw std::invalid_argument("Destination host is empty");

    auto port = value.substr(separator + 1);
    Integer(port, 1, 65535);
    options.host = host;
    options.port = port;
}
} // namespace

Options ParseOptions(int argc, char* const argv[])
{
    Options options{};
    bool hasDestination{};

    for (int i = 1; i < argc; ++i)
    {
        std::string_view argument{argv[i]};

        if (argument == "--help" || argument == "-h")
            options.help = true;
        else if (argument == "--version")
            options.version = true;
        else if (argument == "--keep-local")
            options.mute = false;
        else if (
            argument == "--packet-bytes" || argument == "--buffer-ms" || argument == "--stats" ||
            argument == "--duration" || argument == "--test-tone")
        {
            if (++i == argc)
                throw std::invalid_argument("Missing value for " + std::string{argument});

            std::string_view value{argv[i]};

            if (argument == "--packet-bytes")
            {
                options.packetBytes = Integer(value, 64, 1400);

                if (options.packetBytes % 4)
                    throw std::invalid_argument("Packet bytes must be a multiple of 4 (one stereo frame)");
            }
            else if (argument == "--buffer-ms")
                options.bufferMs = Integer(value, 20, 1000);
            else if (argument == "--stats")
                options.statsSeconds = Integer(value, 0, 3600);
            else if (argument == "--duration")
                options.duration = Decimal(value, 0.01, 86400);
            else
                options.toneHz = Decimal(value, 20, 20000);
        }
        else if (argument.starts_with('-'))
            throw std::invalid_argument("Unknown option: " + std::string{argument});
        else
        {
            if (hasDestination)
                throw std::invalid_argument("Only one destination is supported");

            Destination(argument, options);
            hasDestination = true;
        }
    }

    return options;
}

const char* Usage()
{
    return R"(StrawAudio 0.1.0 — macOS system audio to raw UDP PCM
Usage: strawaudio [host:port] [options]
Default destination: 127.0.0.1:5000
Output: 48000 Hz, stereo, signed 16-bit little-endian; no packet headers

  --keep-local          Keep Mac speakers playing (default: mute while capturing)
  --packet-bytes N      UDP payload, 64..1400, multiple of 4 (default: 1024)
  --buffer-ms N         Input ring capacity, 20..1000 ms (default: 100)
  --stats N             Statistics interval in seconds, 0 disables (default: 5)
  --duration N          Stop after N seconds; default: until Ctrl+C
  --test-tone HZ        Send a -18 dBFS sine wave, without capturing or muting
  --help, -h            Show help without requesting audio permission
  --version            Show version

Use only on a trusted LAN or through a VPN. Raw UDP has no encryption,
receiver acknowledgement, retransmission, timestamps, or clock correction.
)";
}
} // namespace straw
