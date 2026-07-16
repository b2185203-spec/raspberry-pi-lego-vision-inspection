#pragma once

#include <gpiod.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

class Ultrasonic
{
public:
    Ultrasonic(
        gpiod::chip& chip,
        gpiod::line::offset trig,
        gpiod::line::offset echo,
        const std::string& user
    )
        : trig_{trig},
          echo_{echo},
          request_{make_request(chip, trig, echo, user)}
    {
    }

    double measure_cm()
    {
        using clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;

        request_.set_value(trig_, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(2us);

        request_.set_value(trig_, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(20us);
        request_.set_value(trig_, gpiod::line::value::INACTIVE);

        auto wait_start = clock::now();

        while (request_.get_value(echo_) ==
               gpiod::line::value::INACTIVE)
        {
            if (clock::now() - wait_start > 30ms)
            {
                std::cerr << "等待 Echo 超时\n";
                return -1.0;
            }
        }

        auto pulse_start = clock::now();

        while (request_.get_value(echo_) ==
               gpiod::line::value::ACTIVE)
        {
            if (clock::now() - pulse_start > 30ms)
            {
                std::cerr << "Echo 高电平超时\n";
                return -1.0;
            }
        }

        auto pulse_end = clock::now();

        double duration_us =
            std::chrono::duration<double, std::micro>(
                pulse_end - pulse_start
            ).count();

        return duration_us / 58.0;
    }

private:
    static gpiod::line_request make_request(
        gpiod::chip& chip,
        gpiod::line::offset trig,
        gpiod::line::offset echo,
        const std::string& user
    )
    {
        gpiod::line_settings trig_settings;
        trig_settings
            .set_direction(gpiod::line::direction::OUTPUT)
            .set_output_value(gpiod::line::value::INACTIVE);

        gpiod::line_settings echo_settings;
        echo_settings.set_direction(
            gpiod::line::direction::INPUT
        );

        auto builder = chip.prepare_request();

        builder
            .set_consumer(user)
            .add_line_settings(trig, trig_settings)
            .add_line_settings(echo, echo_settings);

        return builder.do_request();
    }

    gpiod::line::offset trig_;
    gpiod::line::offset echo_;
    gpiod::line_request request_;
};