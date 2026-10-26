#include <vector>
#include <expected>
#include <print>
#include <charconv>
#include <ranges>
int main(){ std::expected<int,int> e=1; float f; const char*s="1.5"; std::from_chars(s,s+3,f);
 auto v = std::views::iota(0,3) | std::ranges::to<std::vector>(); std::println("{} {} {}", *e, f, v.size()); }
