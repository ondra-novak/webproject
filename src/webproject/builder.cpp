#include "builder.h"
#include <fstream>
#include <string_view>

std::filesystem::path SearchPaths::find(List SearchPaths::*where, std::string_view name) const {
    for (const auto &p : (this->*where)) {
        std::filesystem::path q = p/name;
        if (std::filesystem::is_regular_file(q)) {
            return q;
        }
    }
    return {};
}

bool PageBuilder::process_file(const std::filesystem::path & src_file, const SearchPaths &paths)
{
    std::filesystem::path context_dir = src_file.parent_path();;

    auto r = _processed.insert(src_file);
    if (!r.second) return false;

    std::string buffer;
    std::ifstream in(src_file);
    int line_number=0;
    while (!!in) {
        std::getline(in, buffer);
        ++line_number;
        std::string_view line = buffer;
        while (!line.empty() && std::isspace(line.front())) line = line.substr(1);
        if (line.compare(0,3,"//#") == 0) {
            auto cmdline = line.substr(3);
            auto sep = cmdline.find(' ');
            if (sep != cmdline.npos) {
                auto cmd = cmdline.substr(0,sep);
                auto param = cmdline.substr(sep+1);
                while (!param.empty() && std::isspace(param.front())) param = param.substr(1);
                while (!param.empty() && std::isspace(param.back())) param = param.substr(0,param.size()-1);
                if (param.size()>1 && param.front() == '"' && param.back() == '"') {
                    param = param.substr(1, param.size()-2);
                }
                
                std::filesystem::path p;
                SearchPaths::List SearchPaths::*section;
                OpenedResources PageBuilder::*resource;
                if (cmd == "require") {
                    section = &SearchPaths::scripts;
                    resource = &PageBuilder::_scripts;
                } else if (cmd == "style") {
                    section = &SearchPaths::styles;
                    resource = &PageBuilder::_styles;
                } else if (cmd == "page") {
                    section = &SearchPaths::page_fragments;
                    resource = &PageBuilder::_page_fragments;
                } else if (cmd == "template") {
                    section = &SearchPaths::page_templates;
                    resource = &PageBuilder::_page_templates;
                } else if (cmd == "header") {
                    section = &SearchPaths::header_fragments;
                    resource = &PageBuilder::_header_fragments;
                } else if (cmd == "resource") {
                    section = &SearchPaths::resources;
                    resource = &PageBuilder::_resources;
                } else {
                    _warning(src_file, line_number, std::string("Unknown directive: ").append(cmd).append(". Only allowed: require, style, page, template, header, resource"));
                    continue;
                }

                p = context_dir/param;
                if (!std::filesystem::is_regular_file(p)) {
                    p = paths.find(section, param);
                }

                if (p == std::filesystem::path()) {
                    _warning(src_file, line_number, std::string("Linked resource was not found: ").append(param));                    
                    continue;
                }

                ++index;

                bool include_file = true;
                if (resource == &PageBuilder::_scripts) {
                    include_file = process_file(p, paths);
                }

                if (include_file) {
                    auto iter = (this->*resource).find(p);
                    if (iter == (this->*resource).end()) {
                        std::string trg ( param);
                        if (!_allocated.insert(trg).second) {
                            auto dot = trg.rfind('.');
                            if (dot == trg.npos) dot = trg.size();
                            trg = trg.substr(0,dot)+"."+std::to_string(index)+trg.substr(dot);
                            _allocated.insert(trg);
                        }
                        (this->*resource).insert(OpenedResources::value_type(p, {trg, index}));
                    }
                }

            }
        }
    }
    return true;
}

void PageBuilder::prepare(const std::filesystem::path &src_file, const SearchPaths &paths)
{
    index=0;
    _page_fragments.clear();
    _header_fragments.clear();
    _scripts.clear();
    _styles.clear();
    _resources.clear();
    _processed.clear();
    _allocated.clear();
    
    process_file(src_file, paths);
    _scripts.insert(OpenedResources::value_type(src_file, {src_file.filename(), index}));
}

void PageBuilder::build(const std::filesystem::path &target_html, BuildMode mode)
{
    auto parent = target_html.parent_path();
    std::filesystem::create_directories(parent);
    build_page(target_html,mode);
    if (mode  != BuildMode::onefile) {
        link_container_files(&PageBuilder::_styles, parent, mode);
        link_container_files(&PageBuilder::_scripts, parent, mode);
    }
    link_container_files(&PageBuilder::_resources, parent, mode);
}


struct EmptyFilter {
    char tmp;
    std::string_view operator()(int c) {
        if (c != EOF) {
            tmp = c;
            return std::string_view(&tmp, 1);
        } else {
            return std::string_view();
        }
    }
};

struct CSSFilter {

    enum Mode {
        comment,
        quotes,
        slash,
        text,
        newline
    };
    Mode mode = text;

    char last = 0;
    char buff[2];
    std::string_view operator()(int c) {
        switch (mode) {
            case Mode::comment:
                switch (c) {
                    case EOF: return {};
                    case '/': if (last == '*') mode = text; last = 0; return {};
                    default: last = c; return {};
                }
            case Mode::quotes:
                switch (c) {
                    case EOF: return {};
                    case '"': if (last != '\\') mode = text; last = c; return std::string_view(&last,1);
                    default: last = c; return std::string_view(&last,1);
                }
            case Mode::slash:
                switch (c) {
                    case EOF: buff[0] = '/'; buff[1] = '\n'; return std::string_view(buff,2);
                    case '/': last = c; return std::string_view(&last,1);
                    case '*': last = 0; mode=comment;return {};
                    case '"': buff[0] = '/'; buff[1] = c; mode = text; mode = quotes; return std::string_view(buff,2);
                    case '\n':last = '/'; mode=newline; return std::string_view(&last,1);
                    default: buff[0] = '/'; buff[1] = c; mode = text; return std::string_view(buff,2);
                }
            case Mode::newline:
                switch (c) {
                    case EOF: return {};
                    case '\n':
                    case '\r': return {};
                    case '/': mode = slash; buff[0] = '\n'; return std::string_view(buff,1);
                    case '"': mode = quotes; buff[0] = '\n';buff[1] = c; return std::string_view(buff,1);
                    default: mode = text;buff[0] = '\n'; buff[1] = c; return std::string_view(buff,2);
                }
            default:
            case Mode::text:
                switch (c) {
                    case EOF: last = '\n'; return std::string_view(&last,1);
                    case '/': mode = slash;  return {};
                    case '\n': 
                    case '\r': mode = newline; return {};
                    case '"': last = c; mode = quotes; return std::string_view(&last,1);
                    default: last = c;  return std::string_view(&last,1);
                }        
        }
    }
};

struct JSFilter {

    enum Mode {
        comment,
        linecomment,
        quotes,
        slash,
        begslash,
        text,
        newline
    };
    Mode mode = text;

    char last = 0;
    char buff[3];
    std::string_view operator()(int c) {
        int idx = 0;
        switch (mode) {
            case Mode::comment:
                switch (c) {
                    case EOF: return {};
                    case '/': if (last == '*') mode = text; last = 0; return {};
                    default: last = c; return {};
                }
            case Mode::linecomment:
                switch (c) {
                    case '\n': mode = newline; return {};
                    default: return {};
                }
            case Mode::quotes:
                switch (c) {
                    case EOF: return {};
                    case '"': if (last != '\\') mode = text; last = c; return std::string_view(&last,1);
                    default: last = c; return std::string_view(&last,1);
                }
            case Mode::begslash:
                buff[0] = '\n';
                idx++;  
                [[fallthrough]];

            case Mode::slash:
                switch (c) {
                    case EOF: buff[idx] = '/'; buff[idx+1] = '\n'; return std::string_view(buff,idx+2);
                    case '/': mode = linecomment;return {};
                    case '*': last = 0; mode=comment;return {};
                    case '"': buff[idx] = '/'; buff[idx+1] = c; mode = text; mode = quotes; return std::string_view(buff,idx+2);
                    case '\n':buff[idx] = '/'; mode=newline; return std::string_view(buff,idx+1);
                    default: buff[idx] = '/'; buff[idx+1] = c; mode = text; return std::string_view(buff,idx+2);
                }
            case Mode::newline:
                switch (c) {
                    case EOF: return {};
                    case ' ':
                    case '\t':
                    case '\n':
                    case '\r': return {};
                    case '/': mode = begslash; return {};
                    case '"': mode = quotes; buff[0] = '\n';buff[1] = c; return std::string_view(buff,1);
                    default: mode = text;buff[0] = '\n'; buff[1] = c; return std::string_view(buff,2);
                }
            default:
            case Mode::text:
                switch (c) {
                    case EOF: buff[0] = ';'; buff[1] = '\n'; return std::string_view(buff,2);
                    case '/': mode = slash;  return {};
                    case '\n': 
                    case '\r': mode = newline; return {};
                    case '"': last = c; mode = quotes; return std::string_view(&last,1);
                    default: last = c;  return std::string_view(&last,1);
                }        
        }
    }
};


template<typename Filter>
static bool append_file(std::ostream &out, std::string fname, Filter &&flt) {
        std::ifstream f(fname);
        if (!f) {
            return false;
        }
        int c = f.get();
        while (c != EOF) {
            out << flt(c);
            c = f.get();
        }
        out << flt(c);
        return true;
}


void PageBuilder::build_page(const std::filesystem::path &target_html, BuildMode mode)
{

    std::filesystem::create_directories(target_html.parent_path());
    std::ofstream out(target_html, std::ios::out|std::ios::trunc);

    std::vector<std::filesystem::path> styles_inline;
    std::vector<std::filesystem::path> scripts_inline;
    std::vector<std::string> styles_link;
    std::vector<std::string> scripts_link;

    auto header = sort_sources(&PageBuilder::_header_fragments);
    auto page = sort_sources(&PageBuilder::_page_fragments);
    auto templates = sort_sources(&PageBuilder::_page_templates);
    if (mode == BuildMode::onefile) {
        styles_inline = sort_sources(&PageBuilder::_styles);
        scripts_inline = sort_sources(&PageBuilder::_scripts);
    } else {
        styles_link = sort_targets(&PageBuilder::_styles);
        scripts_link = sort_targets(&PageBuilder::_scripts);
    }

    out << "<!DOCTYPE html>"
           "<HTML><HEAD>";
    for (const auto &h: header) {
        if (!append_file(out, h, EmptyFilter())) {
            _warning(h,0,"Failed to open file");
            continue;;
        }
    }
    for (const auto &h: styles_link) {
        out << "<LINK rel=\"stylesheet\" href=\"" << h << "\">";
    }
    if (!styles_inline.empty()) {
        out << "<STYLE>\n";
        for (const auto &h: styles_inline) {
            if (!append_file(out, h, CSSFilter())) {
                _warning(h,0,"Failed to open file");
                continue;;
            }
        }
        out << "\n</STYLE>";
    }

    bool has_template = false;

    out << "</HEAD>";
    out << "<BODY>";
    for (const auto &h: templates) {
        auto n = _page_templates.find(h);
        if (n != _page_templates.end()) {
            bool ok;
            out << "<TEMPLATE data-name=\"" << n->second.first << "\">";
            ok = append_file(out, h, EmptyFilter());
            out << "</TEMPLATE>";
            if (!ok) {
                _warning(h,0,"Failed to open file");
                continue;;
            }
            has_template = true;
        }
    }
    for (const auto &h: page) {
        if (!append_file(out, h, EmptyFilter())) {
            _warning(h,0,"Failed to open file");
            continue;;
        }
    }
    out << "<SCRIPT type=\"text/javascript\"><!--\n";
    out << "\"use strict\";\n";

    if (has_template) {
        out << R"javascript(
function loadTemplate(name) {
    var tn = document.querySelector("template[data-name=\""+name+"\"]");
    if (!tn) throw new ReferenceError("Template "+name+" was not imported");
    return document.importNode(tn.content, true);
};
)javascript";
    }

    for (const auto &h: scripts_inline) {
        if (!append_file(out, h, JSFilter())) {
            _warning(h,0,"Failed to open file");
            continue;
        }
        out << ";\n";
    }

    out << "//-->\n</SCRIPT>";


    for (const auto &h: scripts_link) {
        out << "<SCRIPT type=\"text/javascript\" src=\"" << h << "\"></SCRIPT>";
    }

    out << "</BODY></HTML>";

}

std::vector<std::filesystem::path> PageBuilder::sort_sources(OpenedResources PageBuilder::*container)
{
    std::vector<std::filesystem::path> out;
    out.reserve((this->*container).size());
    for (const auto &[s,t]: (this->*container)) out.push_back(s);
    std::sort(out.begin(), out.end(), [&](const std::filesystem::path &a, const std::filesystem::path &b){
        return (this->*container).find(a)->second.second <  (this->*container).find(b)->second.second;
    });
    return out;
}

std::vector<std::string> PageBuilder::sort_targets(OpenedResources PageBuilder::*container)
{
    std::vector<std::pair<std::string,int > > temp;
    temp.reserve((this->*container).size());
    for (const auto &[s,t]: (this->*container)) temp.push_back(t);
    std::sort(temp.begin(), temp.end(), [](const auto &a, const auto &b){return a.second < b.second;});
    std::vector<std::string> out;
    out.reserve(temp.size());
    for ( auto &[s,t]: temp) out.push_back(std::move(s));
    return out;
}

void PageBuilder::link_container_files(OpenedResources PageBuilder::*container, std::filesystem::path target, BuildMode mode)
{
    for (const auto &[src, trg]: (this->*container)){
        auto fulltrg = target / trg.first;
        auto parent = fulltrg.parent_path();
        std::filesystem::create_directories(parent);   
        if (src != fulltrg) {
            std::error_code ec;     
            std::filesystem::remove(fulltrg, ec);
            switch (mode)         {
                default:
                case BuildMode::copy:
                    std::filesystem::copy_file(src,fulltrg,ec);break;
                    break;
                case BuildMode::hardlink:
                    std::filesystem::create_hard_link(src,fulltrg,ec);break;
                    break;
                case BuildMode::symlink:
                    std::filesystem::create_symlink(src,fulltrg,ec);break;
                    break;
            }
            if (ec) {            
                _warning(fulltrg,0,"Failed to link: "+ec.message());
            }
        } else {
            _warning(fulltrg, 0, "skipped, points to the same file");
        }
    }
}
