#include "bootstrap_project_support.hpp"
#include "wood/builtins.hpp"
#include "wood/standard_sources.hpp"
#include <chrono>
#include <iostream>

namespace {
using namespace wood::bootstrap_test;
Json inspect(const fs::path& input) {
    try {
        auto configured=configure(input);
        auto compilation=wood::load_modules(configured.entry,configured.roots);
        auto bound=wood::bind_modules(compilation);
        Json::Array units,symbols;
        for(std::size_t i=0;i<compilation.modules.size();++i) {
            const auto& unit=compilation.modules[i];
            Json::Array imports,namespaces;
            Json::Object names;
            for (std::size_t root=0;root<unit.program.statements.size();++root) {
                const auto& stmt=*unit.program.statements[root];
                if(stmt.kind==wood::Stmt::Kind::Import)
                    imports.emplace_back(Json::Object{{"root",static_cast<Int>(root)},{"target",static_cast<Int>(stmt.resolved_module)}});
                if(stmt.kind==wood::Stmt::Kind::Function || stmt.kind==wood::Stmt::Kind::Record)
                    symbols.emplace_back(Json::Object{{"module",static_cast<Int>(i)},{"root",static_cast<Int>(root)},
                        {"name",stmt.name},{"kind",stmt.kind==wood::Stmt::Kind::Function?"Function":"Record"},
                        {"offset",static_cast<Int>(stmt.location.offset)},{"line",static_cast<Int>(stmt.location.line)},
                        {"column",static_cast<Int>(stmt.location.column)}});
            }
            for(const auto& [name,stmt]:bound.environments[i]) names.emplace(name,static_cast<Int>(stmt->symbol));
            for(const auto& name:bound.namespaces[i]) namespaces.emplace_back(name);
            units.emplace_back(Json::Object{{"file",unit.file},{"package",static_cast<Int>(unit.package)},
                {"source_hex",hex(unit.source)},{"imports",std::move(imports)},{"bindings",std::move(names)},
                {"namespaces",std::move(namespaces)}});
        }
        return reply(diagnostic(),std::move(configured.packages),std::move(units),std::move(symbols),static_cast<Int>(bound.next_symbol));
    } catch(const wood::Diagnostic& e) { return reply(diagnostic(e.code,e.what(),e.file,e.location)); }
    catch(const std::exception& e) { return reply(diagnostic("D5001",e.what(),wood::path_text(input))); }
}
Json manifest(std::string name="app",std::string version="1.0.0",std::string source="src") {
    return Json::Object{{"format",1},{"name",name},{"version",version},{"source",source},
        {"entry","main.wd"},{"dependencies",Json::Object{}}};
}
void set_field(Json& value,const std::string& key,Json replacement) {
    auto object=value.object(); object[key]=std::move(replacement); value=Json(std::move(object));
}
int generate(const fs::path& directory) {
    fs::create_directories(directory);
    Json::Array entries;
    int count=0;
    auto save=[&](const fs::path& path,std::string_view text) {
        fs::create_directories(path.parent_path()); wood::write_file(path,text);
    };
    auto app=[&](std::string_view text) {
        auto root=directory/("case-"+std::to_string(count++));
        save(root/"wood.json",manifest().dump()); save(root/"src/main.wd",text);
        save(root/"src/lib.wd","fn value()->int:\n  return 1\nrecord Item:\n  count: int\nfn _private():\n  pass\n");
        entries.emplace_back(wood::path_text(root/"src/main.wd")); return root;
    };
    for(const auto* text:{"", "import lib\n", "from lib import value,Item\n", "import lib as api\nfrom lib import value as f\n",
        "from lib import _private\n", "from lib import missing\n", "import missing\n", "import lib as print\n",
        "import lib as a\nimport lib as a\n", "import lib as a\nfrom lib import value as a\n",
        "from lib import value as a\nimport lib as a\n", "fn a():\n  pass\nimport lib as a\n",
        "fn x():\n  pass\nrecord x:\n  pass\n", "import std.absent\n", "x=\n", "@\n",
        "import lib as _private\n", "fn run():\n  return missing(1)\n"}) app(text);
    for(const auto& builtin:wood::builtins) app("from lib import value as "+std::string(builtin.name)+"\n");
    for(const char* alias:{"list","bool","void","error"}) app("import lib as "+std::string(alias)+"\n");
    auto root=app("import lib\n"); save(root/"src/lib.wd","value=1\n");
    root=app("import lib\n"); save(root/"src/lib.wd","import nested\nfn f():\n  pass\n"); save(root/"src/nested.wd","print(1)\n");
    root=app("import lib\n"); save(root/"src/lib.wd","import main\n");
    root=app("import lib\n"); save(root/"src/lib.wd","import lib\n");
    root=app("import a\nimport b\n"); save(root/"src/a.wd","import lib\n"); save(root/"src/b.wd","import lib\n");
    root=app("import folder.a\nimport folder.b\n"); save(root/"src/folder/a.wd","from ..lib import value\n"); save(root/"src/folder/b.wd","from .a import value\n");
    root=app("import folder.a\n"); save(root/"src/folder/a.wd","from ..lib import value\n");
    root=app("from ..outside import f\n"); save(root/"outside.wd","fn f():\n  pass\n");
    root=app("from ..src.lib import value\n");
    root=app("import lib\n"); save(root/"src/lib.wd","fn f(:\n  pass\n");
    root=app("import lib\n"); save(root/"src/lib.wd",std::string(16777217,' '));
    for(int length:{128,129}) {
        root=app("import m0\n");
        // Entry + length-1 additional modules, so 128 is accepted, 129 rejected.
        for(int i=0;i<length-1;++i) save(root/("src/m"+std::to_string(i)+".wd"),
            i==length-2 ? "fn f():\n  pass\n" : "import m"+std::to_string(i+1)+"\n");
    }
    for(int size:{1024,1025}) {
        std::string imports;
        root=app("");
        for(int i=0;i<size-1;++i) { imports+="import m"+std::to_string(i)+"\n"; save(root/("src/m"+std::to_string(i)+".wd"),"fn f():\n  pass\n"); }
        save(root/"src/main.wd",imports);
    }
    for(int size:{128,129}) {
        root=app("");
        for(int i=0;i<size;++i) {
            const auto package=i==0 ? root : root/("pkg-"+std::to_string(i));
            auto info=manifest("p"+std::to_string(i));
            if(i<size-1) {
                auto relative=(i==0?"":"../")+std::string("pkg-")+std::to_string(i+1);
                set_field(info,"dependencies",Json::Object{{"dep",Json::Object{{"path",relative},{"version","1.0.0"}}}});
            }
            save(package/"wood.json",info.dump()); save(package/"src/main.wd","");
        }
    }
    // Exact format-1 manifest validation failures.
    for(const auto& bad:std::vector<Json>{Json::Object{},Json::Object{{"format",true}},
        manifest("std"),manifest("bad-name"),manifest("app","01.0.0"),manifest("app","1.0"),manifest("app","1.0.0.")}) {
        root=app(""); save(root/"wood.json",bad.dump());
    }
    for(const auto* text:{"{", "{\"format\":1,\"format\":1}", "{\"unknown\":1}", "[]"}) {
        root=app(""); save(root/"wood.json",text);
    }
    for(const auto* source:{"../escape","missing","/absolute"}) {
        root=app(""); fs::create_directories(root/"../escape"); save(root/"wood.json",manifest("app","1.0.0",source).dump());
    }
    // Dependencies, transitive deduplication, aliases and version errors.
    for(int scenario=0;scenario<8;++scenario) {
        root=app(scenario==0?"import dep\n":"from dep.lib import value\n");
        auto data=manifest();
        set_field(data,"dependencies",Json::Object{{"dep",Json::Object{{"path","dependency"},{"version",scenario==1?"2.0.0":"1.0.0"}}}});
        save(root/"wood.json",data.dump());
        save(root/"dependency/wood.json",manifest("dependency").dump());
        save(root/"dependency/src/lib.wd","fn value()->int:\n  return 2\n");
        if(scenario==2) save(root/"dependency/src/lib.wd","from ..test.hidden import f\n");
        if(scenario==2) { auto dep=manifest("dependency"); set_field(dep,"tests","test"); save(root/"dependency/wood.json",dep.dump()); save(root/"dependency/test/hidden.wd","fn f():\n  pass\n"); }
        if(scenario==3) { auto dep=manifest("dependency"); set_field(dep,"dependencies",Json::Object{{"app",Json::Object{{"path",".."},{"version","1.0.0"}}}}); save(root/"dependency/wood.json",dep.dump()); }
        if(scenario==4) { set_field(data,"dependencies",Json::Object{{"std",Json::Object{{"path","dependency"},{"version","1.0.0"}}}}); save(root/"wood.json",data.dump()); }
        if(scenario==5) { set_field(data,"dependencies",Json::Object{{"dep",Json::Object{{"path","dependency"},{"version","1.0.0"},{"extra",1}}}}); save(root/"wood.json",data.dump()); }
        if(scenario==6) { set_field(data,"dependencies",Json::Object{{"dep",Json::Object{{"path","dependency"},{"version","1.0.0"}}},{"again",Json::Object{{"path","dependency/."},{"version","1.0.0"}}}}); save(root/"wood.json",data.dump()); save(root/"src/main.wd","import dep.lib\nimport again.lib\n"); }
        if(scenario==7) save(root/"dependency/src/lib.wd","import std.time\nfn value()->int:\n  return 2\n");
    }
    root=app(""); auto data=manifest(); set_field(data,"tests","tests"); save(root/"wood.json",data.dump()); save(root/"tests/test_main.wd","from ..src.lib import value\n"); entries.emplace_back(wood::path_text(root/"tests/test_main.wd"));
    root=app(""); entries.emplace_back(wood::path_text(root)); entries.emplace_back(wood::path_text(root/"wood.json"));
    auto standalone=directory/"standalone/main.wd"; save(standalone,"import std.time\nfn f():\n  pass\n"); entries.emplace_back(wood::path_text(standalone));
    root=app("import folder.inside\n"); save(root/"src/real/inside.wd","fn f():\n  pass\n");
#ifndef _WIN32
    fs::create_directory_symlink(root/"src/real",root/"src/folder");
    root=app("import folder.outside\n"); save(root/"outside/outside.wd","fn f():\n  pass\n"); fs::create_directory_symlink(root/"outside",root/"src/folder");
#else
    // Junctions are added by the native PowerShell fixture script without admin.
    save(directory/"junction-inside.txt",wood::path_text(root));
    root=app("import folder.outside\n"); save(root/"outside/outside.wd","fn f():\n  pass\n"); save(directory/"junction-outside.txt",wood::path_text(root));
#endif
    wood::write_file(directory/"entries.json",Json(entries).dump());
    std::cout<<entries.size()<<'\n'; return 0;
}
int guarded(const std::vector<fs::path>& arguments) {
    try {
        if(arguments.size()==2 && arguments[0]=="--verify-stdlib") {
            Json::Object expected;
            for(const auto& [name,text]:wood::standard_sources) expected.emplace(std::string(name),std::string(text));
            if(Json::parse(wood::read_file(arguments[1]))!=Json(expected)) throw std::runtime_error("source bundle differs from embedded standard library");
            return 0;
        }
        if(arguments.size()==3 && arguments[0]=="--measure") {
            const int iterations=std::stoi(wood::path_text(arguments[1]));
            if(iterations<1 || iterations>10000) throw std::runtime_error("iterations must be between 1 and 10000");
            auto counts=[&]() {
                auto input=configure(arguments[2]);
                auto graph=wood::load_modules(input.entry,input.roots);
                auto bound=wood::bind_modules(graph);
                return std::pair(static_cast<Int>(graph.modules.size()),static_cast<Int>(bound.next_symbol-1));
            };
            (void)counts();
            Int modules=0,symbols=0;
            const auto started=std::chrono::steady_clock::now();
            for(int i=0;i<iterations;++i) { auto [m,s]=counts(); modules+=m; symbols+=s; }
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
            std::cout<<Json(Json::Object{{"iterations",iterations},{"modules",modules},{"symbols",symbols},{"elapsed_ns",static_cast<Int>(elapsed)}}).dump()<<'\n';
            return 0;
        }
        if(arguments.size()==2 && arguments[0]=="--stdlib") {
            Json::Object bundle;
            for(const auto& [name,text]:wood::standard_sources) bundle.emplace(std::string(name),std::string(text));
            wood::write_file(arguments[1],Json(bundle).dump()); return 0;
        }
        if(arguments.size()==2 && arguments[0]=="--generate") return generate(arguments[1]);
        if(arguments.empty()) return 2;
        int status=0;
        for(const auto& input:arguments) {
            auto result=inspect(input);
            if(!result.at("diagnostic").at("code").string().empty()) status=1;
            std::cout<<result.dump()<<'\n';
        }
        return status;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 2; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    return guarded(std::vector<fs::path>(argv+1,argv+argc));
}
