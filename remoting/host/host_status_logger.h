// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_HOST_STATUS_LOGGER_H_
#define REMOTING_HOST_HOST_STATUS_LOGGER_H_

#include <map>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "remoting/host/host_status_observer.h"
#include "remoting/protocol/transport.h"
#include "remoting/signaling/log_to_server.h"

namespace remoting {

class HostStatusMonitor;

// HostStatusLogger sends host log entries to a server.
// The contents of the log entries are described in server_log_entry_host.cc.
// They do not contain any personally identifiable information.
class HostStatusLogger : public HostStatusObserver {
 public:
  HostStatusLogger(base::WeakPtr<HostStatusMonitor> monitor,
                  ServerLogEntry::Mode mode,
                  SignalStrategy* signal_strategy,
                  const std::string& directory_bot_jid);
  ~HostStatusLogger() override;

  // Logs a session state change. Currently, this is either
  // connection or disconnection.
  void LogSessionStateChange(const std::string& jid, bool connected);

  // HostStatusObserver interface.
  void OnClientConnected(const std::string& jid) override;
  void OnClientDisconnected(const std::string& jid) override;
  void OnClientRouteChange(const std::string& jid,
                           const std::string& channel_name,
                           const protocol::TransportRoute& route) override;

  // Allows test code to fake SignalStrategy state change events.
  void SetSignalingStateForTest(SignalStrategy::State state);

 private:
  LogToServer log_to_server_;

  base::WeakPtr<HostStatusMonitor> monitor_;

  // A map from client JID to the route type of that client's connection to
  // this host.
  std::map<std::string, protocol::TransportRoute::RouteType>
      connection_route_type_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(HostStatusLogger);
};

}  // namespace remoting

#endif  // REMOTING_HOST_HOST_STATUS_LOGGER_H_
