/******************************************************************************
 * oculus_driver driver library for Blueprint Subsea Oculus sonar.
 * Copyright (C) 2020 ENSTA-Bretagne
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <iostream>
#include <sstream>
using namespace std;

#include <spdlog/spdlog.h>

#include "oculus_driver/SonarDriver.h"
using namespace oculus;


void print_ping(const std::shared_ptr<const oculus::PingMessage>& ping)
{
    static unsigned int count = 0;
    cout << "=============== Got Ping : " << count++ << endl;
    // cout << pingMetadata << endl;
}

void print_dummy(const std::shared_ptr<const oculus::DummyMessage>& msg)
{
    static unsigned int count = 0;
    cout << "=============== Got dummy : " << count++ << endl;
    // cout << msg << endl;
}

int main()
{
    auto ioService = std::make_shared<SonarDriver::IoService>();
    SonarDriver driver(ioService, spdlog::get("console"));

    driver.add_callback<oculus::PingMessage>(oculus::MessageType::PING_MESSAGE, [&](const std::shared_ptr<const oculus::PingMessage>& msg){print_ping(msg);});
    driver.add_callback<oculus::DummyMessage>(oculus::MessageType::DUMMY_MESSAGE, [](const std::shared_ptr<const oculus::DummyMessage>& msg){print_dummy(msg);});

    ioService->run();  // is blocking

    return 0;
}
