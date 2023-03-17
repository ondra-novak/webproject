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


class HttpServer {
public:
    ///Response produced by a handler
    struct Response {
        /// 
        int code;
        ///the data itself
        std::string data;
        ///content type of the data
        std::string_view content_type;
    };

    ///Handler which processes the request
    /**
     * @param 1 string contains path
     */
    using EndpointHandler  = std::function<Response(std::string_view)>;


    ///Initialize server
    /**
     * @param port port number. To use UNIX socket, specify permissions here in decimal base
     *      (so 666 is internally converted to 0666)
     * @param address address of interface to bind , can be empty to bind all interfaces.
     *      prefix "unix:" to bind to unix socket. After "unix:" specify an absolute file
     *      path - "unix:/run/socket"
     * @param hmap list of GET handlers.
     *
     * @note doesn't start worker - you need to call start().
     *
     * @exception system_error - when server cannot be initialized
     */
    HttpServer(int port, std::string address, EndpointHandler h);
    ///Destroys server, stops the thread
    ~HttpServer();

    ///perform HTTP request in connection
    /**
     * @param conn opened connection
     * @param buffer buffer used by function to store temporary data
     * @note you must close connection manually
     *
     * @note MT Safety - function is MT safe, you can call from multiple threads.
     * However it implies that handlers are also MT safe (otherwise MT Safety
     * is limited to MT Safety of handlers)
     *
     */
    void serve(int conn, std::string &buffer) noexcept;

    ///starts the worker.
    /**
     * @note MT Safety - function is not MT Safe. Running worker also
     * conflicts with other MT unsafe functions.
     *
     */
    void start() noexcept;

    ///stops server
    /**
     * This stops worker if it was started. However it also signals stop
     * flag, so further attempt to process connections are rejected and
     * these functions returns false. It also interrupts any blocking
     * operation - including timed operation. So any waiting immediately
     * exits with false state
     *
     * The destructor automatically calls stop()
     *
     * @note MT Safety -  Function is MT Safe. however, when other
     * thread is trying to stop the server, it can return before the
     * stop process is complete, which can falsely indicate, that
     * server can be destroyed. This also applies to situation when one
     * thread is stopping server and other thread is destroying the server.
     */
    void stop() noexcept;

    ///Wait and serve for incomming connection until given time is reached
    /**
     * @param tp time when to stop waiting
     *
     * @note useful in combination of periodic scarping, this can
     * replace sleep command
     *
     * @retval true time reached
     * @retval false server stopped
     *
     * @note Don't combine with start()
     *
     * @note MT Safety - function is NOT MT Safe
     *
     */
    bool perform_until(std::chrono::system_clock::time_point tp) noexcept;

    ///Wait and serve for incomming connections for given period
    /**
     * @param duration duration
     *
     * @note useful in combination of periodic scarping, this can
     * replace sleep command
     *
     * @retval true time reached
     * @retval false server stopped
     *
     * @note Don't combine with start()
     *
     * @note MT Safety - function is NOT MT Safe
     */
    template<typename Duration>
    bool perform_for(Duration dur)  noexcept{
        return perform_until(std::chrono::system_clock::now()+ dur);
    }
    ///Check for incomming connection, process request and exit
    /**
     * @retval true processed
     * @retval false server stopped
     *
     * @note if there is no connection, function immediately returns true. This
     * function is usefull if you perform asynchronous polling the listening socket
     * When the socket is signaled, call this function to process incomming connection
     * and return back to asynchronous polling
     *
     * @note Do not use start() if you plan to do asynchronous polling
     * @note MT Safety - function is NOT MT Safe
     */
    bool perform_now() noexcept;

    ///Retrieves listening socket for purpose of asynchronous polling
    /** @note MT Safety - function is MT Safe */
    int get_listening_socket() const  noexcept{
        return _mother;
    }

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

protected:
    EndpointHandler _h;

    ///server's thread
    std::thread _worker;
    ///mother socket
    int _mother = 0;
    ///stop signal
    std::atomic<bool> _stop = false;
    ///internal buffer for method accept_and_server
    std::string _buffer;
    ///block execution until a connection is accepted, then process serve the content and return
    /**
     * @retval true processed successful
     * @retval false server has been stopped
     */
    bool accept_and_serve() noexcept;

};


#endif