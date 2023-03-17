#pragma once
#ifndef _builder_src_builder_H_33l5L4toO32gojld5kS62qb6aCNCOcn2_
#define _builder_src_builder_H_33l5L4toO32gojld5kS62qb6aCNCOcn2_

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>


enum class BuildMode {
    symlink,
    hardlink,
    copy,
    onefile,
};

struct SearchPaths {

    using List =  std::vector<std::filesystem::path>;

    /// list of folders where to search document fragments
    List page_fragments;
    /// list of folders where to search header fragmets
    List page_templates;
    /// list of folders where to search header fragmets
    List header_fragments;
    /// list of folders where to search javascrips
    List scripts;
    /// styles
    List styles;
    /// resources
    List resources;

    std::filesystem::path find(List SearchPaths::*where, std::string_view name) const;

};

struct PageResources {
    std::string _page_name;
    std::string _target_dir;
    SearchPaths _paths;
};

class PageBuilder {
public:
    using OpenedResources = std::unordered_map<std::filesystem::path, std::pair<std::string, int> >;
    using BlockedNames = std::unordered_set<std::string>;

    using WaringOut = std::function<void(std::string, int, std::string)>;

    PageBuilder(WaringOut wout):_warning(std::move(wout)) {}



    
    bool process_file(const std::filesystem::path & src_file, const SearchPaths &paths);

    void prepare(const std::filesystem::path & src_file, const SearchPaths &paths);

    void build(const std::filesystem::path &target_html, BuildMode mode);


    

protected:
    WaringOut _warning;
    OpenedResources _page_fragments;
    OpenedResources _page_templates;
    OpenedResources _header_fragments;
    OpenedResources _scripts;
    OpenedResources _styles;
    OpenedResources _resources;
    BlockedNames _processed;
    BlockedNames _allocated;
    int index = 0;

    std::vector<std::filesystem::path> sort_sources(OpenedResources PageBuilder::*container);
    std::vector<std::string> sort_targets(OpenedResources PageBuilder::*container);
    void link_container_files(OpenedResources PageBuilder::*container, std::filesystem::path target, BuildMode mode);
    void build_page(const std::filesystem::path &target, BuildMode mode);
};


#endif /* _builder_src_builder_H_33l5L4toO32gojld5kS62qb6aCNCOcn2_ */
