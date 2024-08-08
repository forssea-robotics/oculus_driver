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

#pragma once

#include <eventpp/callbacklist.h>
#include <eventpp/eventdispatcher.h>
#include <eventpp/utilities/argumentadapter.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#include "StatusListener.h"
#include "oculus_driver/Oculus.h"
#include "oculus_driver/OculusMessage.h"
#include "print_utils.h"
#include "utils.h"

namespace oculus {

/**
 * This is the base class to handle an Oculus sonar.
 *
 * It solely handle the network connection to the sonar. Use a subclass such as
 * SonarDriver to control the sonar or receive data.
 *
 * About concurrency on the socket : socket creation, destruction and read all
 * append in the same thread. There is no need to protect the socket for
 * concurrency between these operations. Also, boost sockets allows to be
 * concurrently read and written to at the same time. The situation where a
 * protection is needed is concurrent write and creation/destruction on the
 * socket. Hence, the socket is only locked in the send(), close_connection()
 * and ???. 
 */
class SonarClient
{
    public:

    using IoService    = boost::asio::io_service;
    using IoServicePtr = std::shared_ptr<IoService>;
    using Socket       = boost::asio::ip::tcp::socket;
    using SocketPtr    = std::unique_ptr<Socket>;
    using EndPoint     = boost::asio::ip::tcp::endpoint;
    using Duration     = boost::posix_time::time_duration;

    enum ConnectionState { Initializing, Attempt, Connected, Lost };

    using TimeSource = Message::TimeSource;
    using TimePoint  = Message::TimePoint;
    
    using ErrorCallbacksType = eventpp::CallbackList<void(const boost::system::error_code&)>;
    using ConnectCallbacksType = eventpp::CallbackList<void()>;
    using MessageType = oculus::MessageType;

    private:
    const std::shared_ptr<spdlog::logger> logger;

    protected:
    IoServicePtr       ioService_;


    SocketPtr          socket_;
    EndPoint           remote_;
    uint16_t           sonarId_;
    ConnectionState    connectionState_;
    mutable std::mutex socketMutex_;

    Duration                     checkerPeriod_;
    boost::asio::deadline_timer  checkerTimer_;
    Clock                        clock_;
    
    StatusListener statusListener_;
    // ErrorCallbacksType errorCallbacks;
    // ConnectCallbacksType connectCallbacks;
    eventpp::EventDispatcher<MessageType, void(const std::shared_ptr<const BaseMessage>)> dispatcher;


    template <typename T>
    void dispatch(const std::shared_ptr<const T> msg) {
        dispatcher.dispatch(T::mtype, msg);
    }

    template <typename T, typename... Args>
    typename std::enable_if_t<std::is_constructible_v<T, Args...>> 
            dispatch(Args &&...args) {
        dispatch(std::make_shared<const T> (std::forward<Args>(args)...));
    }
    

    Message::Ptr message_;

    // helper stubs
    void checker_callback(const boost::system::error_code& err);
    void check_reception(const boost::system::error_code& err);

    public:

    SonarClient(const IoServicePtr& ioService,
                const std::shared_ptr<spdlog::logger>& logger,
                const Duration& checkerPeriod = boost::posix_time::seconds(1));

    bool is_valid(const OculusMessageHeader& header);
    bool connected() const;

    size_t send(const boost::asio::streambuf& buffer) const;

    // initialization states
    void reset_connection();
    void close_connection();
    void on_first_status(const OculusStatusMsg& msg);
    void connect_callback(const boost::system::error_code& err);
    virtual void on_connect() = 0;

    // main loop begin
    void initiate_receive();
    void header_received_callback(const boost::system::error_code err,
                                  std::size_t receivedByteCount);
    void data_received_callback(const boost::system::error_code err,
                                std::size_t receivedByteCount);
    
    // This is called regardless of the content of the message.
    // To be reimplemented in a subclass (does nothing by default).
    virtual void handle_message(const Message::ConstPtr& msg) = 0;

    template <typename TimeT = float>
    TimeT time_since_last_message() const { return clock_.now<TimeT>(); }

    TimePoint last_header_stamp() const { return message_->timestamp(); }

    // inline auto& connect_callbacks() {return connectCallbacks; }
    inline auto& status_callbacks() { return statusListener_.callbacks(); }
    // inline auto& error_callbacks() { return errorCallbacks; }

    inline auto& get_dispatcher() { return dispatcher; }


    template <typename T>
    void add_callback(const MessageType message_type,
                        std::function<void(const std::shared_ptr<const T>)> callback) {
        static_assert(std::is_base_of<BaseMessage, T>::value,
                    "T must be derived from BaseMessage");
        dispatcher.appendListener(
            message_type,
            eventpp::argumentAdapter<void(const std::shared_ptr<const T>)>(callback));
    }

    template <typename T>
    bool add_timed_callback(const MessageType message_type,
                            std::function<void(const std::shared_ptr<const T>)> callback, 
                            int timeout_ms = 5000) {
        static_assert(std::is_base_of<BaseMessage, T>::value,
                    "T must be derived from BaseMessage");
        
        std::atomic_flag called;
        auto start = std::chrono::steady_clock::now();

        auto handle = add_counter_callback<T>(message_type, 
            [start, &callback, &called, timeout_ms](const std::shared_ptr<const T> msg) {
                if (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms)) {
                    callback(msg);
                    called.test_and_set();
                }
            }, 1);

        while (!called.test() || std::chrono::steady_clock::now() - start <
                                std::chrono::milliseconds(timeout_ms)) {
            std::this_thread::sleep_for (std::chrono::milliseconds(100));
        }
        dispatcher.removeListener(message_type, handle);
        return called.test();
    }

    template <typename T>
    auto add_counter_callback(const MessageType message_type,
                                std::function<void(const std::shared_ptr<const T>)> callback, 
                                const int triggerCount = 1) {
        static_assert(std::is_base_of<BaseMessage, T>::value,
                    "T must be derived from BaseMessage");
        return eventpp::counterRemover(dispatcher).appendListener(
            message_type,
            eventpp::argumentAdapter<void(const std::shared_ptr<const T>)>(
                callback), triggerCount);
    }

    template <typename T>
    void add_callback(std::function<void(const std::shared_ptr<const T>)> callback) {
        add_callback<T>(T::mtype, callback);
    }

    template <typename T>
    void add_callback(std::initializer_list<MessageType> messages,
                        std::function<void(const std::shared_ptr<const T>)> callback) {
        for (auto message : messages) {
            add_callback<T>(message, callback);
        }
    }

    template <typename T>
    bool add_timed_callback(std::function<void(const std::shared_ptr<const T>)> callback, 
                            int timeout_ms = 5000) {
        return add_timed_callback<T>(T::mtype, callback, timeout_ms);
    }
    
    template <typename T>
    void add_counter_callback(std::function<void(const std::shared_ptr<const T>)> callback, 
                                const int triggerCount = 1) {
        add_counter_callback<T>(T::mtype, callback, triggerCount);
    }
};

}  // namespace oculus
