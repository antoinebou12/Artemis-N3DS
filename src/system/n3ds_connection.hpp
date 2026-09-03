/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "../connection_status.hpp"
#include "../system/AtomicVar.hpp"
#include "../system/subscriber.hpp"
#include <Limelight.h>
#include <memory>
#include <string>

class N3dsConnectionListener : public ISubscriber {
  public:
    N3dsConnectionListener(bool enable_motion);
    ~N3dsConnectionListener();

    void accept(IMessage *msg) override;

    // create_instance() keeps the historical raw-pointer API used by the main
    // stream loop, while get_instance() returns shared ownership. Detached
    // input/dispatcher workers therefore keep the listener alive until they
    // have observed connection_closed and exited.
    static N3dsConnectionListener *create_instance(bool enable_motion) {
        if (instance == nullptr) {
            instance = std::make_shared<N3dsConnectionListener>(enable_motion);
        }
        return instance.get();
    }

    static std::shared_ptr<N3dsConnectionListener> get_instance() {
        return instance;
    }

    static void destroy_instance() {
        if (instance != nullptr) {
            // Wake every worker's loop condition before dropping the global
            // reference. Worker-held shared_ptrs defer destruction safely.
            instance->connection_closed.store(true);
        }
        instance.reset();
    }

    void connection_terminated(int errorCode);
    void connection_log_message(const char *format, va_list arglist);
    void connection_status_update(int status);
    void set_motion_event_state(unsigned short controllerNumber,
                                unsigned char motionType,
                                unsigned short reportRateHz);

    bool is_connection_closed();
    std::string termination_user_message() const;

  private:
    static std::shared_ptr<N3dsConnectionListener> instance;
    bool enable_motion;
    AtomicVar<bool> debug = false;
    AtomicVar<bool> connection_closed = false;
    int last_termination_code = ML_ERROR_GRACEFUL_TERMINATION;
    std::string last_termination_message;
};

extern CONNECTION_LISTENER_CALLBACKS n3ds_connection_callbacks;
