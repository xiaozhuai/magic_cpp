//
// Copyright (c) 2025 xiaozhuai
//

#include <iostream>
#include <string_view>
#include <type_traits>

template <size_t N>
struct StringStream {
public:
    constexpr void push(char c) { data[size++] = c; }
    [[nodiscard]] constexpr const char *c_str() const { return data; }
    [[nodiscard]] constexpr std::string_view string_view() const { return std::string_view(data, size); }
    constexpr operator const char *() const { return data; }
    constexpr operator std::string_view() const { return std::string_view(data, size); }

    size_t size{};
    char data[N]{};
};

struct DummyStream {
    size_t size{};
    constexpr void push(char) { ++size; }
};

template <typename Stream>
constexpr auto parse(const char *input, bool skip, char *cells, size_t &pc, Stream &&output) -> size_t {
    const char *c = input;
    while (*c) {
        switch (*c) {
            case '+':
                if (!skip) ++cells[pc];
                break;
            case '-':
                if (!skip) --cells[pc];
                break;
            case '.':
                if (!skip) output.push(cells[pc]);
                break;
            case '>':
                if (!skip) ++pc;
                break;
            case '<':
                if (!skip) --pc;
                break;
            case '[': {
                while (!skip && cells[pc] != 0) parse(c + 1, false, cells, pc, output);
                c += parse(c + 1, true, cells, pc, output) + 1;
            } break;
            case ']':
                return c - input;
            default:
                break;
        }
        ++c;
    }
    return c - input;
}

template <typename Stream>
constexpr auto parse(const char *input, Stream &&output) -> Stream && {
    constexpr size_t cell_size = 128;  // Size of the memory cells
    char cells[cell_size]{};
    size_t pc{};
    parse(input, false, cells, pc, output);
    return output;
}

template <size_t output_size>
constexpr auto brain_fuck_exec(const char *input) {
    StringStream<output_size> stream;
    return parse(input, stream);
}

constexpr auto brain_fuck_output_size(const char *input) -> size_t {
    DummyStream stream;
    return parse(input, stream).size + 1;  // include '\0'
}

#define brain_fuck(in) (brain_fuck_exec<brain_fuck_output_size(in)>(in))

int main() {
    constexpr auto result = brain_fuck(R"(
>++++++++[<+++++++++>-]<.                 ; H
>>++++++++++[<++++++++++>-]<+.            ; e
>>+++++++++[<++++++++++++>-]<.            ; l
>>+++++++++[<++++++++++++>-]<.            ; l
>>++++++++++[<+++++++++++>-]<+.           ; o
>>++++[<++++++++>-]<.                     ;
>>+++++++++++[<++++++++>-]<-.             ; W
>>++++++++++[<+++++++++++>-]<+.           ; o
>>++++++++++[<++++++++++++>-]<------.     ; r
>>+++++++++[<++++++++++++>-]<.            ; l
>>++++++++++[<++++++++++>-]<.             ; d
>>++++++[<++++++>-]<---.                  ; !
)");
    static_assert(result.string_view() == "Hello World!");
    std::cout << result << std::endl;
    return 0;
}
