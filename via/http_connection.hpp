#pragma once

#ifndef HTTP_CONNECTION_HPP_VIA_HTTPLIB_
#define HTTP_CONNECTION_HPP_VIA_HTTPLIB_
//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013 Ken Barker
// (ken dot barker at via-technology dot co dot uk)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////
#include "via/http/request.hpp"
#include "via/http/response.hpp"
#include "via/comms/connection.hpp"
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/signal.hpp>
#include <deque>
#include <iostream>

namespace via
{
  ////////////////////////////////////////////////////////////////////////////
  /// @class http_connection
  ///
  ////////////////////////////////////////////////////////////////////////////
  template <typename SocketAdaptor, typename Container = std::vector<char> >
  class http_connection : public boost::enable_shared_from_this
          <http_connection<SocketAdaptor, Container> >
  {
  public:

    typedef comms::connection<SocketAdaptor, Container> connection_type;

    typedef typename boost::weak_ptr<http_connection<SocketAdaptor, Container> >
       weak_pointer;

    typedef typename boost::shared_ptr<http_connection<SocketAdaptor, Container> >
       shared_pointer;

    typedef typename Container::const_iterator Container_const_iterator;

  private:

    boost::weak_ptr<connection_type> connection_;
    http::request_receiver<Container> rx_;

    /// Constructor.
    http_connection(boost::weak_ptr<connection_type> connection) :
      connection_(connection),
      rx_()
    {}

    bool send(Container const& packet)
    {
      bool keep_alive(rx_.request().keep_alive());
      rx_.clear();

      boost::shared_ptr<connection_type> tcp_pointer(connection_.lock());
      if (tcp_pointer)
      {
        tcp_pointer->send_data(packet);

        if (keep_alive)
          return tcp_pointer->read_pending();
        else
          tcp_pointer->disconnect();

        return false;
      }
      else
        std::cerr << "http_connection::send connection weak pointer expired"
                  << std::endl;
      return false;
    }

  public:

    /// Create.
    static shared_pointer create(boost::weak_ptr<connection_type> connection)
    {
      return shared_pointer(new http_connection(connection));
    }

    http::rx_request const& request() const
    { return rx_.request(); }

    Container_const_iterator body_begin() const
    { return rx_.body().begin(); }

    Container_const_iterator body_end() const
    { return rx_.body().end(); }

    bool receive()
    {
      // attempt to get the pointer
      boost::shared_ptr<connection_type> tcp_pointer(connection_.lock());
      if (!tcp_pointer)
        return false;

      // attempt to read the data
      while (tcp_pointer->read_pending())
      {
        Container data(tcp_pointer->read_data());
        http::receiver_parsing_state rx_state
            (rx_.receive(data.begin(), data.end()));

        if (rx_state == http::RX_VALID)
          return true;
        else
        {
          if (rx_state == http::RX_INVALID)
          {
            http::tx_response response(http::response_status::BAD_REQUEST, 0);
            send(response);
          }

          return false;
        }
      }

      return false;
    }

    bool send(http::tx_response& response)
    {
      response.set_major_version(rx_.request().major_version());
      response.set_minor_version(rx_.request().minor_version());
      std::string http_header(response.message());
      Container tx_message(http_header.begin(), http_header.end());
      return send(tx_message);
    }

    template<typename ForwardIterator1, typename ForwardIterator2>
    bool send(http::tx_response& response,
              ForwardIterator1 begin, ForwardIterator2 end)
    {
      response.set_major_version(rx_.request().major_version());
      response.set_minor_version(rx_.request().minor_version());
      std::string http_header(response.message());

      size_t size(end - begin);
      Container tx_message;
      tx_message.reserve(http_header.size() + size);
      tx_message.assign(http_header.begin(), http_header.end());
      tx_message.insert(tx_message.end(), begin, end);
      return send(tx_message);
    }

    void disconnect()
    { connection_.lock()->disconnect(); }

  };

}
#endif
