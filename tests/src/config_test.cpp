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

#include "oculus_driver/OculusMessage.h"
#include <iostream>
#include <sstream>
#include <thread>
using namespace std;

#include <spdlog/spdlog.h>

#include "oculus_driver/AsyncService.h"
#include "oculus_driver/SonarDriver.h"
using namespace oculus;


void print_ping(const std::shared_ptr<const oculus::PingMessage>& ping)
{
    cout << "=============== Got Ping :" << endl;
    cout << ping->header() << endl;
    cout << ping->gain_percent() << endl;
}

void print_dummy(const std::shared_ptr<const oculus::DummyMessage>& msg)
{
    cout << "=============== Got dummy :" << endl;
    cout << msg->header << endl;
}


int main()
{
    // Sonar sonar;
    AsyncService ioService;
    SonarDriver sonar(ioService.io_service(), spdlog::get("console"));

    sonar.add_callback<oculus::PingMessage>(oculus::MessageType::PING_MESSAGE, [&](const std::shared_ptr<const oculus::PingMessage>& msg){print_ping(msg);});
    sonar.add_callback<oculus::DummyMessage>(oculus::MessageType::DUMMY_MESSAGE, [](const std::shared_ptr<const oculus::DummyMessage>& msg){print_dummy(msg);});

    ioService.start();

    // sonar.request_fire_config(default_fire_config());
    // sonar.request_fire_config(default_fire_config());

    getchar();

    ioService.stop();

    return 0;
}
