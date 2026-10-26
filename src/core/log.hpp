#pragma once
#include <format>
#include <string_view>

namespace tto {

void log_line(std::string_view level, std::string_view msg);

template <class... A>
void log_info(std::format_string<A...> f, A&&... a) { log_line("info", std::format(f, std::forward<A>(a)...)); }
template <class... A>
void log_warn(std::format_string<A...> f, A&&... a) { log_line("warn", std::format(f, std::forward<A>(a)...)); }
template <class... A>
void log_error(std::format_string<A...> f, A&&... a) { log_line("error", std::format(f, std::forward<A>(a)...)); }

}  // namespace tto
