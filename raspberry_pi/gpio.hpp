#pragma once

#include <gpiod.hpp>
#include <string>

class GPIO
{
public: GPIO(
            gpiod::chip& chip, 
            gpiod::line::offset offset, 
            bool is_output, 
            const std::string& user
            )
        : offset_(offset),
          request_(make_request(chip, offset, is_output, user))
          
    {}

    void set(int value)
    {
        request_.set_value(
            offset_,
            value == 1
                ? gpiod::line::value::ACTIVE
                : gpiod::line::value::INACTIVE
        );
    }


private:
    static gpiod::line_request make_request(
        gpiod::chip& chip,
        gpiod::line::offset offset,
        bool is_output,
        const std::string& user
    )
    {
        gpiod::line_settings settings;

        if (is_output)
        {
            settings.set_direction(gpiod::line::direction::OUTPUT);
            settings.set_output_value(gpiod::line::value::INACTIVE);
        }
        else
        {
            settings.set_direction(gpiod::line::direction::INPUT);
        }

        auto builder = chip.prepare_request();
        builder.set_consumer(user);
        builder.add_line_settings(offset, settings);

        return builder.do_request();
    }

    gpiod::line::offset offset_;
    gpiod::line_request request_;
};