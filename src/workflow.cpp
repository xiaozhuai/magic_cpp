//
// Copyright (c) 2026 xiaozhuai
//

#include "workflow.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main() {
    std::vector<std::string> lifecycle;
    std::vector<std::string> results;
    // clang-format off
    auto workflow = make_workflow<int>()
        .then(JobConfig{
            .on_task = [&lifecycle](int value) -> std::optional<int> {
                lifecycle.emplace_back("task");
                if (value == 3) {
                    return std::nullopt;
                }
                return value * 2;
            },
            .on_start = [&lifecycle] {
                lifecycle.emplace_back("start");
            },
            .on_finish = [&lifecycle] {
                lifecycle.emplace_back("finish");
            },
            .input_queue = {
                .capacity = 16,
                .full_policy = QueueFullPolicy::drop_oldest,
            }})
        .then([](int value) { return "value=" + std::to_string(value + 1); })
        .sink([&results](std::string value) {
            results.push_back('[' + value + ']');
        });
    // clang-format on

    workflow.start();
    for (int value = 0; value < 8; ++value) {
        const auto submitted = workflow.submit(value);
        assert(submitted == SubmitResult::accepted);
        (void)submitted;
    }
    const auto submitted_to_job_1 = workflow.submit<1>(100);
    const auto submitted_to_job_2 = workflow.submit<2>("direct");
    assert(submitted_to_job_1 == SubmitResult::accepted);
    assert(submitted_to_job_2 == SubmitResult::accepted);
    (void)submitted_to_job_1;
    (void)submitted_to_job_2;
    workflow.close();
    workflow.wait();

    assert(lifecycle.front() == "start");
    assert(lifecycle.back() == "finish");

    std::vector<std::string> expected{
        "[value=1]",  "[value=3]",  "[value=5]",   "[value=9]", "[value=11]",
        "[value=13]", "[value=15]", "[value=101]", "[direct]",
    };
    std::ranges::sort(results);
    std::ranges::sort(expected);
    assert(results == expected);

    for (const auto &result : results) {
        std::cout << result << '\n';
    }
    return 0;
}
