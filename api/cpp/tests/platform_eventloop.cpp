// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-1.1 OR LicenseRef-Slint-commercial

#include <optional>
#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include <slint_platform.h>
#include <thread>
#include <deque>
#include <memory>
#include <mutex>

namespace slint_platform = slint::experimental::platform;

struct TestPlatform : slint_platform::Platform
{
    std::mutex the_mutex;
    std::deque<slint_platform::PlatformEvent> queue;
    bool quit = false;
    std::condition_variable cv;

    /// Returns a new WindowAdapter
    virtual std::unique_ptr<slint_platform::WindowAdapter> create_window_adapter() const override
    {
        assert(!"creating window in this test");
        return nullptr;
    };

    /// Spins an event loop and renders the visible windows.
    virtual void run_event_loop() override
    {
        while (true) {
            std::optional<slint_platform::PlatformEvent> event;
            {
                std::unique_lock lock(the_mutex);
                if (queue.empty()) {
                    if (quit) {
                        quit = false;
                        break;
                    }
                    cv.wait(lock);
                    continue;
                } else {
                    event = std::move(queue.front());
                    queue.pop_front();
                }
            }
            if (event) {
                std::move(*event).invoke();
                event.reset();
            }
        }
    }

    /// Exits the event loop.
    ///
    /// This is what is called by slint::quit_event_loop() and can be called from a different thread
    /// or re-enter from the event loop
    virtual void quit_event_loop() override
    {
        const std::unique_lock lock(the_mutex);
        quit = true;
        cv.notify_all();
    }

    /// Invokes the event from the event loop.
    ///
    /// This function is called by slint::invoke_from_event_loop().
    /// It can be called from any thread, but the passed function must only be called
    /// from the event loop.
    /// Reimplements this function and move the event to the event loop before calling
    /// PlatformEvent::invoke()
    virtual void invoke_from_event_loop(slint_platform::PlatformEvent event) override
    {
        const std::unique_lock lock(the_mutex);
        queue.push_back(std::move(event));
        cv.notify_all();
    }
};

bool init_platform = (TestPlatform::register_platform(std::make_unique<TestPlatform>()), true);

TEST_CASE("Quit from event")
{
    int called = 0;
    slint::invoke_from_event_loop([&] {
        slint::quit_event_loop();
        called += 10;
    });
    REQUIRE(called == 0);
    slint::run_event_loop();
    REQUIRE(called == 10);
}

TEST_CASE("Event from thread")
{
    std::atomic<int> called = 0;
    auto t = std::thread([&] {
        called += 10;
        slint::invoke_from_event_loop([&] {
            called += 100;
            slint::quit_event_loop();
        });
    });

    slint::run_event_loop();
    REQUIRE(called == 110);
    t.join();
}

TEST_CASE("Blocking Event from thread")
{
    std::atomic<int> called = 0;
    auto t = std::thread([&] {
        // test returning a, unique_ptr because it is movable-only
        std::unique_ptr foo =
                slint::blocking_invoke_from_event_loop([&] { return std::make_unique<int>(42); });
        called = *foo;
        int xxx = 123;
        slint::blocking_invoke_from_event_loop([&] {
            slint::quit_event_loop();
            xxx = 888999;
        });
        REQUIRE(xxx == 888999);
    });

    slint::run_event_loop();
    REQUIRE(called == 42);
    t.join();
}
