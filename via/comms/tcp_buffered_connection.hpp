#pragma once

#ifndef TCP_BUFFERED_CONNECTION_HPP_VIA_HTTPLIB_
#define TCP_BUFFERED_CONNECTION_HPP_VIA_HTTPLIB_
//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013 Ken Barker
// (ken dot barker at via-technology dot co dot uk)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////
#include "buffered_connection.hpp"
#include <boost/asio.hpp>
#include <boost/bind.hpp>

namespace via
{
  namespace comms
  {
    //////////////////////////////////////////////////////////////////////////
    /// @class tcp_buffered_connection
    /// This class
    //////////////////////////////////////////////////////////////////////////
    template <typename Container>
    class tcp_buffered_connection : public buffered_connection<Container>
    {
      typedef buffered_connection<Container> buffered_connection_class;

      boost::asio::io_service& io_service_;

      /// The TCP socket.
      boost::asio::ip::tcp::socket socket_;

      size_t connection_timeout_;
      size_t receive_timeout_;
      boost::asio::deadline_timer deadline_timer_;

      /// Resolve the ip address of the connection.
      /// @param hostName the name of the TCP host
      /// @param portName the name of the TCP port
      /// @return a resolver iterator to the potential endpoints
      boost::asio::ip::tcp::resolver::iterator resolve_host
          (char const* hostName, char const* portName) const
      {
        boost::asio::ip::tcp::resolver resolver(io_service_);
        boost::asio::ip::tcp::resolver::query query(hostName, portName);
        boost::asio::ip::tcp::resolver::iterator itr(resolver.resolve(query));
        return itr;
      }

      ///
      bool connect_socket(boost::asio::ip::tcp::resolver::iterator itr)
      {
        if (boost::asio::ip::tcp::resolver::iterator() == itr)
          return false;

        // Attempt to connect to the host
        socket_.async_connect(*itr,
                  boost::bind(&tcp_buffered_connection::handle_connect, this,
                              boost::asio::placeholders::error, itr));

        if (connection_timeout_)
        {
          deadline_timer_.expires_from_now
            (boost::posix_time::milliseconds(connection_timeout_));
          deadline_timer_.async_wait
            (boost::bind(&tcp_buffered_connection::connection_timeout, this,
                         boost::asio::placeholders::error));
        }

        return true;
      }

      ///
      void handle_connect(boost::system::error_code const& error,
                          boost::asio::ip::tcp::resolver::iterator itr)
      {
        // if a connection has been established
        if (!error)
        {
          deadline_timer_.cancel();
          set_no_delay(true);
          buffered_connection_class::enable_reception();
          connection::signal_connected();
          start_receive_timer();
        }
        else // there was an error
        {
          // try a different host?
          if ((boost::asio::error::host_not_found == error) &&
              (boost::asio::ip::tcp::resolver::iterator() != itr))
          {
            socket_.close();
            connect_socket(++itr);
          }
          else
          {
            stop();
            connection::signal_error(error);
          }
        }
      }

      /// Close the socket and signal a timeout.
      void connection_timeout(boost::system::error_code const& error)
      {
        if (boost::asio::error::operation_aborted != error)
        {
          stop();
          connection::signal_connection_timedout();
        }
      }

      ///
      void start_receive_timer()
      {
        if (receive_timeout_)
        {
          deadline_timer_.expires_from_now
            (boost::posix_time::milliseconds(receive_timeout_));
          deadline_timer_.async_wait
            (boost::bind(&tcp_buffered_connection::receive_timedout, this,
                         boost::asio::placeholders::error));
        }
      }

      ///
      void receive_timedout(boost::system::error_code const& error)
      {
        if (boost::asio::error::operation_aborted != error)
          connection::signal_receive_timedout();
      }

    protected:

      /// test whether the socket has been disconnected
      void signal_error(const boost::system::error_code& error)
      {
        if (boost::asio::error::connection_reset == error)
          connection::signal_disconnected();
        else
          connection::signal_error(error);
      }

      /// Read the next packet received on the socket into the buffer.
      /// Note: it calls the socket_.async_read_some
      /// function NOT boost::asio::async_read.
      /// This is because async_read_some calls the read_handler as soon as it
      /// has received a packet, whilst async_read only calls the read_handler
      /// when it has filled the receive buffer...
      ///
      /// See boost::asio documentation on "Short Reads and Short Writes".
      ///
      /// @param ptr a pointer to the start of the read buffer.
      /// @param size the size of the read buffer in bytes.
      void read(void* ptr, size_t& size)
      {
        socket_.async_read_some
            (boost::asio::buffer(ptr, size),
             boost::bind(&tcp_buffered_connection::read_handler, this,
                         boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));
      }

      /// Send the packet at the front of the queue.
      /// Note: calls the boost::asio::async_write function NOT
      /// socket_.async_write_some for similar reasons to those described for
      /// the read function above.
      ///
      /// See boost::asio documentation on "Short Reads and Short Writes".
      ///
      /// @param ptr a pointer to the start of the write buffer.
      /// @param size the size of the write buffer in bytes.
      void write(void const* ptr, size_t size)
      {
        boost::asio::async_write
            (socket_, boost::asio::buffer(ptr, size),
             boost::bind(&tcp_buffered_connection::write_handler, this,
             boost::asio::placeholders::error,
             boost::asio::placeholders::bytes_transferred));
      }

      ///
      virtual void write_handler(const boost::system::error_code& error,
                                      size_t bytes_transferred)
      {
        buffered_connection_class::write_handler(error, bytes_transferred);
        start_receive_timer();
      }

      ///
      void stop()
      {
        deadline_timer_.cancel();
        boost::system::error_code ignoredEc;
        socket_.shutdown (boost::asio::ip::tcp::socket::shutdown_both,
                          ignoredEc);
        socket_.close(ignoredEc);
      }

      /// Constructor.
      /// Hidden to ensure that it can only be TODO...
      explicit tcp_buffered_connection(boost::asio::io_service& io_service,
                                       size_t receive_timeout,
                                       size_t connection_timeout,
                                       size_t buffer_size) :
        buffered_connection_class(buffer_size),
        io_service_(io_service),
        socket_(io_service),
        connection_timeout_(connection_timeout),
        receive_timeout_(receive_timeout),
        deadline_timer_(io_service)
      {}

    public:

      static const size_t DEFAULT_RECEIVE_BUFFER_SIZE = 4096;

      ///
      static boost::shared_ptr<tcp_buffered_connection> create
                                      (boost::asio::io_service& io_service,
                                       size_t receive_timeout = 0,
                                       size_t connection_timeout = 0,
                                       size_t buffer_size =
                                                 DEFAULT_RECEIVE_BUFFER_SIZE)
      {
        return boost::shared_ptr<tcp_buffered_connection>
          (new tcp_buffered_connection
            (io_service, receive_timeout, connection_timeout, buffer_size));
      }

      ///
      bool connect(const char* hostName, const char* portName)
      { return connect_socket(resolve_host(hostName, portName)); }

      void set_no_delay(bool no_delay = true)
      {
 //       socket_.set_option(boost::asio::socket_base::send_buffer_size
 //                          (MAX_RX_PACKET_SIZE));
//        socket_.set_option(boost::asio::socket_base::receive_buffer_size
 //                          (MAX_RX_PACKET_SIZE));
        socket_.set_option(boost::asio::ip::tcp::no_delay(no_delay));
      }

      ///
      boost::asio::ip::tcp::socket& socket()
      { return socket_; }

    };

  }
}
#endif