#include "core/log.hpp"

#include <chrono>
#include <cstdio>

namespace tto {

void log_line(std::string_view level, std::string_view msg) {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - t0).count();
    std::fprintf(stderr, "[%8.3f] %-5.*s %.*s\n", t, static_cast<int>(level.size()), level.data(),
                 static_cast<int>(msg.size()), msg.data());
}

}  // namespace tto
