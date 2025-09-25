//
// Copyright (c) 2025 xiaozhuai
//

#include <iostream>
#include <string_view>
#include <utility>

namespace concat {

template <typename CharT, size_t N>
struct StrLiteral {
    size_t size = N;
    CharT data[N + 1]{};
};

template <typename CharT, const std::basic_string_view<CharT> &...strs>
struct impl {
    static constexpr auto get_data() {
        constexpr size_t N = (strs.size() + ... + 0);
        StrLiteral<CharT, N> result{};
        size_t offset = 0;
        ([&] {
            for (auto c : strs) {
                result.data[offset++] = c;
            }
        }(), ...);
        result.data[N] = '\0';
        return result;
    }
    static constexpr auto data = get_data();
    static constexpr std::basic_string_view<CharT> value{data.data, data.size};
};

template <typename CharT, const std::basic_string_view<CharT> &...strs>
static constexpr auto concat = impl<CharT, strs...>::value;

template <const std::basic_string_view<char> &...strs>
static constexpr auto string = concat<char, strs...>;

template <const std::basic_string_view<wchar_t> &...strs>
static constexpr auto wstring = concat<wchar_t, strs...>;

}  // namespace concat

namespace resources {

namespace strings {
constexpr std::string_view empty = concat::string<>;
constexpr std::string_view hello = "Hello";
constexpr std::string_view space = " ";
constexpr std::string_view world = "World";
constexpr std::string_view exclamation = "!";
constexpr std::string_view hello_world = concat::string<hello, space, world, exclamation>;
}  // namespace strings

namespace wstrings {
constexpr std::wstring_view empty = concat::wstring<>;
constexpr std::wstring_view hello = L"Hello";
constexpr std::wstring_view space = L" ";
constexpr std::wstring_view world = L"World";
constexpr std::wstring_view exclamation = L"!";
constexpr std::wstring_view hello_world = concat::wstring<hello, space, world, exclamation>;
}  // namespace wstrings

}  // namespace resources

template <typename CharT>
constexpr CharT end_of_string_view(const std::basic_string_view<CharT> &sv) noexcept {
    return *(sv.data() + sv.size());
}

int main() {
    {
        static_assert(resources::strings::empty.empty());
        static_assert(end_of_string_view(resources::strings::empty) == '\0');
    }
    {
        static_assert(resources::strings::hello_world == "Hello World!");
        static_assert(end_of_string_view(resources::strings::hello_world) == '\0');
        std::cout << resources::strings::hello_world << std::endl;
    }
    {
        static_assert(resources::wstrings::empty.empty());
        static_assert(end_of_string_view(resources::wstrings::empty) == '\0');
    }
    {
        static_assert(resources::wstrings::hello_world == L"Hello World!");
        static_assert(end_of_string_view(resources::wstrings::hello_world) == '\0');
        std::wcout << resources::wstrings::hello_world << std::endl;
    }
    return 0;
}
