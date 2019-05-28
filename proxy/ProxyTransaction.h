/** @file

  ProxyTransaction - Base class for protocol client/server transactions.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once

#include "ProxySession.h"
#include <string_view>

class HttpSM;
class Http1ServerSession;

// Abstract Class for any transaction with-in the HttpSM
class ProxyTransaction : public VConnection
{
public:
  ProxyTransaction();

  /// Virtual Methods
  //
  virtual void new_transaction();
  virtual void attach_server_session(Http1ServerSession *ssession, bool transaction_done = true);
  virtual void transaction_done() = 0;
  virtual void release(IOBufferReader *r); ///< Indicate we are done with this transaction
  virtual void destroy();

  /// Virtual Accessors
  //
  virtual NetVConnection *get_netvc() const;
  virtual void set_parent(ProxySession *new_parent);

  virtual void increment_client_transactions_stat() = 0;
  virtual void decrement_client_transactions_stat() = 0;

  virtual void set_active_timeout(ink_hrtime timeout_in)     = 0;
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;
  virtual void cancel_inactivity_timeout()                   = 0;

  virtual bool is_first_transaction() const;
  virtual bool is_chunked_encoding_supported() const;
  virtual void set_session_active();
  virtual void clear_session_active();

  virtual in_port_t get_outbound_port() const;
  virtual IpAddr get_outbound_ip4() const;
  virtual IpAddr get_outbound_ip6() const;
  virtual void set_outbound_port(in_port_t port);
  virtual void set_outbound_ip(const IpAddr &new_addr);
  virtual bool is_outbound_transparent() const;
  virtual void set_outbound_transparent(bool flag);
  virtual void set_h2c_upgrade_flag();
  virtual bool allow_half_open() const = 0;
  virtual const char *get_protocol_string();
  virtual int populate_protocol(std::string_view *result, int size) const;
  virtual const char *protocol_contains(std::string_view tag_prefix) const;
  virtual int get_transaction_id() const = 0;

  /// Non-Virtual Methods
  //
  Action *adjust_thread(Continuation *cont, int event, void *data);

  /// Non-Virtual Accessors
  //
  HttpSM *get_sm() const;
  ProxySession *get_parent() const;
  Http1ServerSession *get_server_session() const;

  bool is_transparent_passthrough_allowed();
  void set_half_close_flag(bool flag);
  bool get_half_close_flag() const;

  HostResStyle get_host_res_style() const;
  void set_host_res_style(HostResStyle style);

  bool debug() const;

  APIHook *ssn_hook_get(TSHttpHookID id) const;
  bool has_hooks() const;

  const IpAllow::ACL &get_acl() const;

  void set_restart_immediate(bool val);
  bool get_restart_immediate() const;

  void set_rx_error_code(ProxyError e);
  void set_tx_error_code(ProxyError e);

  /// Variables
  //
protected:
  ProxySession *proxy_ssn   = nullptr;
  HttpSM *current_reader    = nullptr;
  IOBufferReader *sm_reader = nullptr;

  /// DNS resolution preferences.
  HostResStyle host_res_style;
  /// Local outbound address control.
  IpAddr outbound_ip4;
  IpAddr outbound_ip6;
  in_port_t outbound_port{0};

  bool restart_immediate;

private:
};
