#ifndef UDP_ADAPTOR_HPP_VIA_HTTPLIB_
#define UDP_ADAPTOR_HPP_VIA_HTTPLIB_

#pragma once

//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2015 Ken Barker
// (ken dot barker at via-technology dot co dot uk)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////
/// @file udp_adaptor.hpp
/// @brief Contains the udp_adaptor socket adaptor class.
//////////////////////////////////////////////////////////////////////////////
#include "socket_adaptor.hpp"
#include "via/no_except.hpp"

namespace via
{
  namespace comms
  {
    //////////////////////////////////////////////////////////////////////////
    /// @class udp_adaptor
    /// This class enables the connection class to use udp sockets.
    /// This class together with tcp_adaptor and ssl_tcp_adaptor provide a
    /// copmmon interface that enables connection to be configured for udp,
    /// tcp or ssl sockets.
    /// @see connection
    /// @see tcp_adaptor
    /// @see ssl::ssl_tcp_adaptor
    //////////////////////////////////////////////////////////////////////////
    class udp_adaptor
    {
      boost::asio::io_service& io_service_;        ///< The asio io_service.
      boost::asio::ip::udp::socket socket_;        ///< The asio UDP socket.
      boost::asio::ip::udp::endpoint rx_endpoint_; ///< The receive endpoint.
      boost::asio::ip::udp::endpoint tx_endpoint_; ///< The transmit endpoint.
      bool is_connected_; ///< The socket is connected (i.e. not bound).

    protected:

      /// @fn handshake
      /// Performs the SSL handshake. Since this isn't an SSL socket, it just
      /// calls the handshake_handler with a success error code.
      /// @param handshake_handler the handshake callback function.
      // @param is_server whether performing client or server handshaking,
      // not used by un-encrypted sockets.
      void handshake(ErrorHandler handshake_handler, bool /*is_server*/ = false)
      {
        boost::system::error_code ec; // Default is success
        handshake_handler(ec);
      }

      /// @fn connect_socket
      /// Performs the TCP connection.
      /// Since this isn't a TCP socket, it just calls the connect_handler
      /// with a success error code.
      /// @param connect_handler the connect callback function.
      /// @param host_iterator the tcp resolver iterator.
      void connect_socket(ConnectHandler connect_handler,
                          boost::asio::ip::tcp::resolver::iterator host_iterator)
      {
        boost::system::error_code ec; // Default is success
        connect_handler(ec, host_iterator);
      }

      /// @fn read
      /// The udp socket read function.
      /// @param ptr pointer to the receive buffer.
      /// @param size the size of the receive buffer.
      /// @param read_handler the handler for received messages.
      void read(void* ptr, size_t size, CommsHandler read_handler)
      {
        if (is_connected_)
          socket_.async_receive(boost::asio::buffer(ptr, size), read_handler);
        else
          socket_.async_receive_from(boost::asio::buffer(ptr, size),
                                     rx_endpoint_, read_handler);
      }

      /// @fn write
      /// The udp socket write function.
      /// @param buffers the buffer(s) containing the message.
      /// @param write_handler the handler called after a message is sent.
      void write(ConstBuffers& buffers, CommsHandler write_handler)
      {
        if (is_connected_)
          socket_.async_send(buffers, write_handler);
        else
          socket_.async_send_to(buffers, tx_endpoint_, write_handler);
      }

      /// The udp_adaptor constructor.
      /// @param io_service the asio io_service associted with this connection
      explicit udp_adaptor(boost::asio::io_service& io_service)
        : io_service_(io_service)
        , socket_(io_service_)
        , rx_endpoint_(boost::asio::ip::address_v4::any(), 0)
        , tx_endpoint_(boost::asio::ip::address_v4::broadcast(), 0)
        , is_connected_(false)
      {}

    public:

      /// A virtual destructor because connection inherits from this class.
      virtual ~udp_adaptor()
      {}

      /// The default size of the receive buffer.
      static const size_t DEFAULT_RX_BUFFER_SIZE = 2048;

      /// Enable multicast reception on the given port_number and address.
      /// @param port_number the UDP port
      /// @param multicast_address the multicast address to receive from.
      /// @param listen_address the local address for the local endpoint.
      /// @return true if successful, false otherwise.
      bool receive_multicast(unsigned short port_number,
                             std::string const& multicast_address,
                             std::string listen_address = "")
      {
        // Get the address
        boost::system::error_code error;
        boost::asio::ip::address ip_address
            (boost::asio::ip::address::from_string(multicast_address, error));
        if (error)
          return false;

        // Ensure that it's a multicast address
        if (!ip_address.is_multicast())
          return false;

        // if a listen_address is given then use it
        boost::asio::ip::address local_address;
        if (!listen_address.empty())
        {
          local_address = boost::asio::ip::address
              (boost::asio::ip::address::from_string(listen_address, error));
          // Ensure that the listen_address is valid and the same protocol as
          // the multicast_address
          if (error || (local_address.is_v6() != ip_address.is_v6()))
            return false;

          rx_endpoint_ = boost::asio::ip::udp::endpoint(local_address, port_number);
        }
        else // otherwise use the IPv4 or IPv6 any address
        {
          if (ip_address.is_v6())
            rx_endpoint_ = boost::asio::ip::udp::endpoint
                              (boost::asio::ip::address_v6::any(), port_number);
          else
            rx_endpoint_ = boost::asio::ip::udp::endpoint
                              (boost::asio::ip::address_v4::any(), port_number);
        }

        // Open the socket
        if (!socket_.is_open())
          socket_.open(rx_endpoint_.protocol(), error);
        if (error)
          return false;

        // Set it for reuse then bind it
        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket_.bind(rx_endpoint_, error);
        if (error)
          return false;

        if (!listen_address.empty())
          // Note: asio supports only v4 at the moment.
          socket_.set_option(boost::asio::ip::multicast::join_group
                               (ip_address.to_v4(), local_address.to_v4()));
        else
          socket_.set_option(boost::asio::ip::multicast::join_group(ip_address));

        return true;
      }

      /// Transmit on the given port_number in broadcast or multicast mode.
      /// @post Call connection::set_connected to enable transmission.
      /// @param port_number the UDP port
      /// @param multicast_address the multicast address to send to.
      /// @return true if successful, false otherwise.
      bool transmit_multicast(unsigned short port_number,
                              std::string const& multicast_address)
      {
        // Get the address
        boost::system::error_code error;
        boost::asio::ip::address ip_address
            (boost::asio::ip::address::from_string(multicast_address, error));
        if (error)
          return false;

        // Ensure that it's a multicast address
        if (!ip_address.is_multicast())
          return false;

        // Set the transmit endpoint
        tx_endpoint_ = boost::asio::ip::udp::endpoint(ip_address, port_number);

        // Open the socket
        if (!socket_.is_open())
          socket_.open(tx_endpoint_.protocol(), error);
        if (error)
          return false;

        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));

        return true;
      }

      /// Enable broadcast reception on the given port_number.
      /// @pre UDP Broadcast is only available for IPv4.
      /// @param port_number the UDP port
      /// @return true if successful, false otherwise.
      bool receive_broadcast(unsigned short port_number)
      {
        // Set the receive port number
        rx_endpoint_.port(port_number);

        // Open the socket (Ipv4)
        boost::system::error_code error;
        if (!socket_.is_open())
          socket_.open(rx_endpoint_.protocol(), error);
        if (error)
          return false;

        // Set it for reuse then bind it
        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket_.bind(rx_endpoint_, error);

        return !error;
      }

      /// Enable broadcast transmission on the given port_number.
      /// @pre UDP Broadcast is only available for IPv4.
      /// @post Call connection::set_connected to enable transmission.
      /// @param port_number the UDP port
      /// @return true if successful, false otherwise.
      bool transmit_broadcast(unsigned short port_number)
      {
        tx_endpoint_.port(port_number);

        // Open the socket (Ipv4)
        boost::system::error_code error;
        if (!socket_.is_open())
          socket_.open(rx_endpoint_.protocol(), error);
        if (error)
          return false;

        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket_.set_option(boost::asio::socket_base::broadcast(true));

        return true;
      }

      /// @fn connect
      /// Connect the udp socket to the given host name and port.
      /// @pre To be called by "connected" connections only.
      /// Multicast and Broadcast sockets must NOT call this function.
      /// @param host_name the host to connect to.
      /// @param port the port to connect to.
      /// @param connectHandler the handler to call when connected.
      bool connect(const char* host_name, const char* port ,
                   ConnectHandler connectHandler)
      {
        // resolve the port on the local host
        boost::asio::ip::udp::resolver resolver(io_service_);
        boost::asio::ip::udp::resolver::query query(host_name, port );
        boost::asio::ip::udp::resolver::iterator iterator(resolver.resolve(query));

        // Determine whether the host and port was found
        if (iterator == boost::asio::ip::udp::resolver::iterator())
          return false;

        // Attempt to connect to it.
        boost::system::error_code error;
        socket_.connect(*iterator, error);
        if (error)
          return false;

        is_connected_ = true;

        // Call the connectHandler with the success error code.
        // Note: uses a tcp resolver::iterator because the connectHandler
        // requires it.
        connectHandler(error, boost::asio::ip::tcp::resolver::iterator());

        return true;
      }

      /// @fn shutdown
      /// The udp socket shutdown function.
      /// Disconnects the socket.
      /// @param write_handler the handler to notify that the socket is
      /// disconnected.
      void shutdown(CommsHandler write_handler)
      {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::udp::socket::shutdown_both, ec);

        ec = boost::system::error_code(boost::asio::error::eof);
        write_handler(ec, 0);
      }

      /// @fn close
      /// The udp socket close function.
      /// Cancels any send, receive or connect operations and closes the socket.
      void close()
      {
        boost::system::error_code ignoredEc;
        if (socket_.is_open())
          socket_.close (ignoredEc);
      }

      /// @fn start
      /// The tcp socket start function.
      /// Signals that the socket is connected.
      /// @param handshake_handler the handshake callback function.
      void start(ErrorHandler handshake_handler)
      {
        boost::system::error_code ec; // Default is success
        handshake_handler(ec);
      }

      /// @fn is_disconnect
      /// This function determines whether the error is a socket disconnect.
      // @param error the error_code
      // @retval ssl_shutdown - an ssl_disconnect should be performed
      /// @return true if a disconnect error, false otherwise.
      bool is_disconnect(boost::system::error_code const&, bool&) NOEXCEPT
      { return false; }

      /// @fn socket
      /// Accessor for the underlying udp socket.
      /// @return a reference to the udp socket.
      boost::asio::ip::udp::socket& socket() NOEXCEPT
      { return socket_; }
    };
  }
}

#endif // UDP_ADAPTOR_HPP_VIA_HTTPLIB_
