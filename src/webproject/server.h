#pragma once
#ifndef _builder_src_server_H_33l5L4toO32gojld5kS62qb6aCNCOcn2_
#define _builder_src_server_H_33l5L4toO32gojld5kS62qb6aCNCOcn2_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <functional>
#include <stop_token>


class HttpServer {
public:

    class Request {
    public:

        Request(std::string_view path, int socket):path(path),socket(socket) {}
        Request(Request &&other):path(other.path),socket(other.socket){other.socket = -1;}
        ~Request() {
            if (socket != -1) {
                send(204, "No content","","");
            }
        }
        void send(int code, std::string_view message, std::string_view content_type, std::string_view data);
        void send(int code, std::string_view message, std::string_view content_type, std::istream &data);

        std::string_view path;
    protected:        
        int socket;                
    };


    using EndpointHandler  = std::function<void(Request &req)>;


    HttpServer(int port, std::string address, EndpointHandler h);
    ///Destroys server, stops the thread
    ~HttpServer();

    void serve(int conn, std::string &buffer) noexcept;




    ///Server status/error page
    /**
     * @param buffer temporary buffer (should be preallocated)
     * @param conn connected socket
     * @param status_line status line, must be in correct form, "<code> <message>
     *    for example "404 Not found". Otherwise invalid response can be produced
     * @param extra_msg extra message put to error page. Note that content type
     * of the body is text/plain, so no http formatting is allowed
     *
     * @note you need to close connection manually
     *
     * @note MT Safety - function is MT Safe
     */
    static void send_status(std::string &buffer, int conn, std::string_view status_line, std::string_view extra_msg = std::string_view())  noexcept;

    ///block execution until a connection is accepted, then process serve the content and return
    /**
     * @retval true processed successful
     * @retval false server has been stopped
     */
    bool accept_and_serve() noexcept;


    void run(std::stop_token stop_token);


protected:
    EndpointHandler _h;

    ///mother socket
    int _mother = 0;

};


#endif