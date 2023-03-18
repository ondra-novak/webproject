#include "webproject.h"
#include "builder.h"
#include "server.h"

#include <webproject_version.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <chrono>

enum class SetMode {
    input,
    path,
    output,
    mode,
    server
};

void show_help() {
    std::cout << "Usage: webproject <switches> source_file.js\n\n"
        "-h (--help)               Show help\n"
        "-I <path>                 Add search path for scripts\n"
        "-C <path>                 Add search path for styles\n"
        "-H <path>                 Add search path for header fragments\n"
        "-T <path>                 Add search path for page templates\n"
        "-F <path>                 Add search path for page fragments\n"
        "-o <path/index.html>      Set output html page\n"
        "-s <addr:port>            Start server at addr:port (for example localhost:10000)"
        "-m <build mode>           Select build mode\n"
        "           s,symlink        -link all linkable resources by symlinks\n"
        "           h,hardlink       -link all linkable resources by hardlinks\n"
        "           c,copy           -copy all linkable resources\n"
        "           p,onepage        -create one page with inline styles and scripts\n";
}

int main(int argc, char **argv) {    

    PageBuilder bld([](std::string file, int line, std::string msg){
        std::cerr << file << ":" << line << " warning: " << msg << std::endl;
    });


    std::string out_path;
    std::string in_path;
    std::string server_addr;
    BuildMode build_mode = BuildMode::onefile;
    SetMode set_mode = SetMode::input;
    SearchPaths::List SearchPaths::*cur_path= nullptr;
    SearchPaths srch;
    int arg = 1;
    while (arg < argc) {
        std::string_view a;
        if (argv[arg][0] == '-') {
            if (set_mode != SetMode::input) {
                std::cerr << "Expects argument: " << argv[arg] << std::endl;
                return 1;
            }
            char c = argv[arg][1];
            switch (c) {
                case 'I': cur_path = &SearchPaths::scripts;set_mode = SetMode::path;break;
                case 'R': cur_path = &SearchPaths::resources;set_mode = SetMode::path;break;
                case 'C': cur_path = &SearchPaths::styles;set_mode = SetMode::path;break;
                case 'H': cur_path = &SearchPaths::header_fragments;set_mode = SetMode::path;break;
                case 'T': cur_path = &SearchPaths::page_templates;set_mode = SetMode::path;break;
                case 'F': cur_path = &SearchPaths::page_fragments;set_mode = SetMode::path;break;
                case 's': set_mode = SetMode::server;break;
                case 'o': set_mode = SetMode::output;break;
                case 'm': set_mode = SetMode::mode;break;
                case 'h': show_help();return 0;break;
                default: std::cerr << "unknown switch -" <<  c << std::endl; return 1;
            }
            a = argv[arg]+2;
            if (a.empty()) {
                ++arg;
                continue;
            }
        }  else {
            a = argv[arg];
            if (a == "--help") {
                show_help(); return 0;
            }
        }
        switch (set_mode) {
            case SetMode::path: (srch.*cur_path).push_back(std::string(a));break;
            case SetMode::mode: 
                if (a == "s" || a == "symlink") build_mode = BuildMode::symlink;
                else if (a == "h" || a == "hardlink") build_mode = BuildMode::hardlink;
                else if (a == "c" || a == "copy") build_mode = BuildMode::copy;
                else if (a == "o" || a == "p" || a == "onefile") build_mode = BuildMode::onefile;
                else {
                    std::cerr << "Invalid buildmode:  " << a << " is not in (symlink, hardlink, copy, onefile)" << std::endl;
                    return 1;
                }
                break;
            case SetMode::input:
                if (in_path.empty()) in_path = a;
                else {
                    std::cerr << "Input file is already set:" << in_path << std::endl;
                    return 1;
                }
                break;
            default:
            case SetMode::server:
                if (server_addr.empty()) server_addr = a;
                else {
                    std::cerr << "Server address already set:" << server_addr << std::endl;
                    return 1;
                }
                break;
            case SetMode::output:    
                if (out_path.empty()) out_path = a;
                else {
                    std::cerr << "Output path is already set:" << out_path << std::endl;
                    return 1;
                }
                break;            
        }
        set_mode = SetMode::input;
        ++arg;
    }

    if (in_path.empty()) {
        std::cerr << "Missing arguments, use -h for help" << std::endl;
        return 2;
    }

    if (out_path.empty()) {
        std::cerr << "Target directory is not specified (use -o <target>)" << std::endl;return 4;
    }
    auto input_path = std::filesystem::absolute(in_path);
    auto output_path = std::filesystem::absolute(out_path);

    try {

        bld.prepare(input_path, srch);
        bld.build(output_path,build_mode);

        if (!server_addr.empty()) {
            auto sep = server_addr.rfind(':');
            if (sep == server_addr.npos) {
                std::cerr << "Server address has no port. Failed to start server" << std::endl;return 5;
            }
            int port = std::atoi(server_addr.c_str()+sep+1);
            if (port == 0) {
                std::cerr << "Invalid port address. Failed to start server" << std::endl;return 6;
            }
            auto base_dir = output_path.parent_path();
            HttpServer server(port,server_addr.substr(0,sep), [&](HttpServer::Request &req) {
                auto path = req.path;                
                auto q = path.find('?');
                if (q != path.npos) path = path.substr(0,q);
                q = path.find('/');
                auto file_path = base_dir;
                while (q != path.npos) {
                    std::string_view part = path.substr(0,q);
                    path = path.substr(q+1);
                    if (!part.empty() && part != "." && part != "..") {
                        file_path = file_path/part;
                    }
                    q = path.find('/');
                }
                if (!path.empty() && path != "." && path != "..") {
                    file_path = file_path/path;
                }
                if (file_path == base_dir) {
                    file_path = output_path;
                }
                if (file_path == output_path) {
                    bld.build(out_path,build_mode);                    
                } 
                std::ifstream in((std::string(file_path)));
                if (!in) {
                    std::cout << "GET " << req.path << " -> " << file_path.string() << " NOT FOUND!" << std::endl;
                    req.send(404,"Not found","text/plain","Not found");
                    return;
                }

                std::string ext = file_path.extension();

                std::string_view content_type;
                if (ext == ".html" || ext == ".htm") content_type = "text/html;charset=utf-8";
                else if (ext == ".css")  content_type = "text/css;charset=utf-8";
                else if (ext == ".js")  content_type = "text/javascript;charset=utf-8";
                else if (ext == ".png")  content_type = "image/png";
                else if (ext == ".jpg")  content_type = "image/jpeg";
                else if (ext == ".jpeg")  content_type = "image/jpeg";
                else if (ext == ".gif")  content_type = "image/gif";
                else if (ext == ".svg")  content_type = "image/svg+xml";
                else content_type = "application/octet-stream";

                std::cout << "GET " << req.path << " -> " << file_path.string() << " " << content_type << std::endl;


                req.send(200,"OK",content_type, in);
            });
            std::cout << "Server started at http://" << server_addr << "/ . Press Ctrl-C to stop" <<  std::endl;
            do {
                //exit by ctrl+c;
                server.run({});
            } while (true);
        }

    } catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
    }

    return 0;
}
