#include "strawaudio/Session.hpp"
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted{};

void OnSignal(int)
{
    // 핸들러는 플래그만 갱신한다. 정리는 일반 스레드에서 RAII로 처리한다.
    interrupted = 1;
}

void PrintStats(straw::Stats stats, double elapsed)
{
    auto kilobytesPerSecond = static_cast<double>(stats.sentBytes) / elapsed / 1000;
    std::cerr << std::fixed << std::setprecision(1) << "[" << elapsed << "s] sent=" << stats.sentBytes << " bytes/"
              << stats.sentPackets << " packets, avg=" << kilobytesPerSecond << " kB/s"
              << ", overruns=" << stats.overruns << ", input_drop=" << stats.droppedFrames
              << ", stale_drop=" << stats.staleFrames << ", udp_drop=" << stats.droppedPackets
              << ", udp_errors=" << stats.sendErrors << ", non_silent=" << stats.nonSilentFrames << " frames\n";
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        auto options = straw::ParseOptions(argc, argv);

        if (options.help)
        {
            std::cout << straw::Usage();
            return 0;
        }

        if (options.version)
        {
            std::cout << "StrawAudio 0.1.0\n";
            return 0;
        }

        struct sigaction action{};
        action.sa_handler = OnSignal;
        sigemptyset(&action.sa_mask);

        auto interruptStatus = sigaction(SIGINT, &action, nullptr);
        auto terminateStatus = sigaction(SIGTERM, &action, nullptr);

        if (interruptStatus || terminateStatus)
            throw std::runtime_error("Cannot install termination signal handlers");

        straw::Session session{options};
        std::cerr << "Starting " << options.host << ':' << options.port << " -> 48000 Hz / stereo / s16le\n";

        if (options.toneHz == 0)
        {
            std::cerr << "Allow system audio recording when macOS prompts.\n";

            if (options.mute)
                std::cerr << "Local playback will be muted while capturing.\n";
            else
                std::cerr << "Local playback is preserved (--keep-local).\n";
        }

        session.Start();
        auto started = std::chrono::steady_clock::now();
        auto nextStats = started + std::chrono::seconds{options.statsSeconds};
        bool warned{};
        std::string failure{};
        std::cerr << "Running. Input=" << session.InputSampleRate() << " Hz, input ring=" << session.RingBytes()
                  << " bytes. Ctrl+C stops transmission.\n";

        while (!interrupted)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>{now - started}.count();
            failure = session.Failure();

            if (!failure.empty() || (options.duration > 0 && elapsed >= options.duration))
                break;

            if (options.statsSeconds && now >= nextStats)
            {
                auto stats = session.GetStats();
                PrintStats(stats, elapsed);
                nextStats = now + std::chrono::seconds{options.statsSeconds};
            }

            if (!warned && elapsed >= 5)
            {
                auto stats = session.GetStats();

                if (!stats.capturedFrames || !stats.nonSilentFrames)
                    std::cerr << "No non-silent audio yet. Play audio; if it stays silent, check system audio "
                                 "recording permission and restart.\n";

                warned = true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }

        session.Stop();
        auto elapsed = std::chrono::duration<double>{std::chrono::steady_clock::now() - started}.count();
        auto stats = session.GetStats();
        PrintStats(stats, std::max(elapsed, 0.001));

        if (!failure.empty())
            throw std::runtime_error(failure);

        std::cerr << "Stopped. Capture resources released. UDP delivery is not acknowledged by the receiver.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "strawaudio: " << exception.what() << '\n';
        return 1;
    }
}
