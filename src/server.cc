// Copyright 2012, Evan Klitzke <evan@eklitzke.org>

#include "./server.h"

#include <boost/lexical_cast.hpp>

#include <iostream>
#include <sstream>
#include <utility>

#include "./connection.h"
#include "./request.h"
#include "./response.h"

namespace garfield {
HTTPServer::HTTPServer(boost::asio::io_service *io_service)
    :io_service_(*io_service), acceptor_(*io_service) {
}
void HTTPServer::AddRoute(const std::string &route, Handler handler) {
  routes_.push_back(std::make_pair(boost::regex(route), handler));
}

void HTTPServer::Bind(int port) {
  boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
  acceptor_.bind(endpoint);
  acceptor_.listen();
  ScheduleAccept();
}

void HTTPServer::ScheduleAccept() {
  Connection *conn = new Connection(
      new boost::asio::ip::tcp::socket(io_service_),
      std::bind(&HTTPServer::OnRequest, this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));

  acceptor_.async_accept(
      *conn->sock(),
      std::bind(&HTTPServer::OnNewConnection, this, conn,
                std::placeholders::_1));
}

void HTTPServer::OnNewConnection(Connection *conn,
                                 const boost::system::error_code& error) {
  conn->NotifyConnected();
  ScheduleAccept();
}

void HTTPServer::OnRequest(Connection *conn, Request *req, RequestError err) {
  if (err != OK) {
    delete conn;
    delete req;
    return;
  }
  bool found = false;
  Handler handler;
  for (auto route : routes_) {
    if (boost::regex_match(req->path, route.first)) {
      handler = route.second;
      found = true;
      break;
    }
  }
  if (found) {
    Response *resp = new Response();;
    resp->headers()->AddHeader("Server", "garfield/0.1");
    resp->headers()->AddHeader("Connection", "close");
    resp->headers()->AddHeader("Content-Type", "text/html");
    handler(req, resp);

    std::string response_header = "HTTP/1.1 ";
    response_header += boost::lexical_cast<std::string>(resp->status());
    response_header += " ";
    response_header += resp->GetStatusName();
    response_header += "\r\n";

    std::size_t bytes = 0;
    std::vector<boost::asio::const_buffer> send_bufs;
    send_bufs.push_back(boost::asio::buffer(response_header, response_header.size()));
    send_bufs.push_back(boost::asio::buffer("\r\n", 2));
    for (const std::string chunk : resp->output()) {
      send_bufs.push_back(boost::asio::buffer(chunk, chunk.size()));
      bytes += chunk.size();
    }
    resp->headers()->SetHeader("Content-Length", boost::lexical_cast<std::string>(bytes));
    std::string hdrs = resp->headers()->GetHeadersAsString();
    send_bufs.insert(send_bufs.begin() + 1, boost::asio::buffer(hdrs, hdrs.size()));

    std::size_t expected_size = 0;
    for (const boost::asio::const_buffer &buf : send_bufs) {
      expected_size += boost::asio::buffer_size(buf);
    }

    boost::asio::async_write(*conn->sock(), send_bufs,
                             std::bind(&HTTPServer::OnWrite, this, conn, req,
                                       resp, expected_size,
                                       std::placeholders::_1,
                                       std::placeholders::_2));
  }
}

void HTTPServer::OnWrite(Connection *conn, Request *req, Response *resp,
                         std::size_t expected_size,
                         const boost::system::error_code& error,
                         std::size_t bytes_transferred) {
  assert(!error);
  assert(expected_size == bytes_transferred);
  delete conn;
  delete req;
  delete resp;
}
}
