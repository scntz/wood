#include "wood/compiler.hpp"
#include "wood/support.hpp"
#include "wood/builtins.hpp"
#include <iostream>

namespace {
using wood::Json;
using Int=std::int64_t;
Json inspect(const std::string& source) {
    try {
        wood::Compilation graph;
        wood::ModuleUnit unit;unit.file="query.wd";unit.source=source;
        unit.program=wood::parse(wood::lex(source));graph.modules.push_back(std::move(unit));
        wood::analyze(graph);
        Json::Array results;
        for(const auto& stmt:graph.modules[0].program.statements)
            if(stmt->kind==wood::Stmt::Kind::Function) results.emplace_back(wood::type_name(stmt->return_type));
        for(const auto& stmt:graph.modules[0].program.statements)
            if(stmt->kind==wood::Stmt::Kind::Record) for(const auto& method:stmt->body)
                results.emplace_back(wood::type_name(method->return_type));
        return Json::Object{{"code",""},{"message",""},{"offset",0},{"line",0},{"column",0},{"results",std::move(results)}};
    } catch(const wood::Diagnostic& error) {
        return Json::Object{{"code",error.code},{"message",error.what()},
            {"offset",static_cast<Int>(error.location.offset)},{"line",static_cast<Int>(error.location.line)},
            {"column",static_cast<Int>(error.location.column)},{"results",Json::Array{}}};
    }
}
bool supported_type(const wood::Type& type) {
    if(type.element && !supported_type(*type.element)) return false;
    if(type.key && !supported_type(*type.key)) return false;
    for(const auto& item:type.parameters) if(!supported_type(item)) return false;
    return true;
}
bool supported_statement(const wood::Stmt& stmt);
bool supported_expression(const wood::Expr& expr) {
    if(expr.function && !supported_statement(*expr.function)) return false;
    for(const auto& child:expr.children) if(child && !supported_expression(*child)) return false;
    return true;
}
bool supported_statement(const wood::Stmt& stmt) {
    if(!supported_type(stmt.return_type) || !supported_type(stmt.annotation)) return false;
    for(const auto& parameter:stmt.parameters) if(!supported_type(parameter.type)) return false;
    for(const auto& expr:stmt.expressions) if(!supported_expression(*expr)) return false;
    for(const auto& child:stmt.body) if(!supported_statement(*child)) return false;
    for(const auto& child:stmt.otherwise) if(!supported_statement(*child)) return false;
    return true;
}
int guarded(const std::vector<wood::fs::path>& args) {
    if(args.size()==1 && args[0]=="--native-contract") {
        Json::Object result;
        for(const auto& builtin:wood::builtins)
            result.emplace(std::string(builtin.name),Json::Object{{"native",std::string(builtin.native_name)},{"site",builtin.site}});
        std::cout<<Json(std::move(result)).dump()<<'\n';return 0;
    }
    if(args.size()==1 && args[0]=="--native-names") {
        Json::Array names;for(const auto& builtin:wood::builtins) names.emplace_back(std::string(builtin.name));
        std::cout<<Json(names).dump()<<'\n';return 0;
    }
    if(args.size()==3 && args[0]=="--filter") {
        const auto all=Json::parse(wood::read_file(args[1]));Json::Array accepted;std::size_t excluded=0;
        for(const auto& query:all.array()) {
            bool supported=true;
            try {
                auto program=wood::parse(wood::lex(query.at("source").string()));
                for(const auto& stmt:program.statements) supported=supported && supported_statement(*stmt);
            } catch(const wood::Diagnostic&) {} // Parser failures are valid differential cases.
            if(supported) accepted.push_back(query);else ++excluded;
        }
        wood::write_file(args[2],Json(accepted).dump());std::cout<<accepted.size()<<" accepted, "<<excluded<<" outside profile\n";return 0;
    }
    if(args.size()==3 && args[0]=="--extend") {
        auto queries=Json::parse(wood::read_file(args[1])).array();
        const auto extra=Json::parse(wood::read_file(args[2]));
        for(const auto& item:extra.array()) queries.push_back(item);
        for(int count:{64,65}) {
            std::string source;
            for(int i=0;i<count;++i) source+="fn f"+std::to_string(i)+"():\n    return "+(i+1==count?"1":"f"+std::to_string(i+1)+"()")+"\n";
            queries.emplace_back(Json::Object{{"source",std::move(source)}});
        }
        wood::write_file(args[1],Json(queries).dump());std::cout<<queries.size()<<'\n';return 0;
    }
    if(args.size()==2 && args[0]=="--comprehension-limits") {
        auto queries=Json::parse(wood::read_file(args[1])).array();
        for(const auto& iterable:{"range(1)","[0]","'x'","{0:0}"}) {
            for(int count:{64,65}) {
                std::string source="x=[0";
                for(int i=0;i<count;++i)
                    source+=" for x"+std::to_string(i)+" in "+iterable;
                queries.emplace_back(Json::Object{{"source",source+"]\n"}});
            }
        }
        for(int count:{63,64}) {
            std::string source="x=[i for i in range(1)";
            for(int i=0;i<count;++i) source+=" if true";
            queries.emplace_back(Json::Object{{"source",source+"]\n"}});
        }
        wood::write_file(args[1],Json(queries).dump());std::cout<<queries.size()<<'\n';return 0;
    }
    if(args.size()==2 && args[0]=="--closure-limits") {
        auto queries=Json::parse(wood::read_file(args[1])).array();
        for(int count:{255,256}) {
            std::string source,head="[";
            for(int i=0;i<count;++i) {
                source+="v"+std::to_string(i)+"="+std::to_string(i)+"\n";
                if(i) head+=",";
                head+="v"+std::to_string(i);
            }
            queries.emplace_back(Json::Object{{"source",source+"f=fn()=>"+head+"]\n"}});
        }
        for(int count:{64,65}) {
            std::string source="f=";
            for(int i=0;i<count;++i) source+="fn()=>";
            queries.emplace_back(Json::Object{{"source",source+"1\n"}});
        }
        for(int count:{255,256}) {
            std::string source="record R0:\n    value:int\nx0=R0(0)\n";
            for(int i=1;i<count;++i) {
                const auto current=std::to_string(i),previous=std::to_string(i-1);
                source+="record R"+current+":\n    value:R"+previous+"\nx"+current+"=R"+current+"(x"+previous+")\n";
            }
            queries.emplace_back(Json::Object{{"source",source+"f=fn()=>x"+std::to_string(count-1)+"\n"}});
        }
        wood::write_file(args[1],Json(queries).dump());std::cout<<queries.size()<<'\n';return 0;
    }
    if(args.size()==2 && args[0]=="--generic-limits") {
        auto queries=Json::parse(wood::read_file(args[1])).array();
        for(int count:{1024,1025}) {
            std::string source;
            for(int i=0;i<count;++i)
                source+="fn f"+std::to_string(i)+"[T](value:T)->T:\n    return value\n";
            for(int i=0;i<count;++i) source+="f"+std::to_string(i)+"(1)\n";
            queries.emplace_back(Json::Object{{"source",std::move(source)}});
        }
        queries.emplace_back(Json::Object{{"source","fn grow[T](value:T)->int:\n    return grow([value])\ngrow(1)\n"}});
        wood::write_file(args[1],Json(queries).dump());std::cout<<queries.size()<<'\n';return 0;
    }
    if(args.size()!=1) return 2;
    const auto queries=Json::parse(wood::read_file(args[0]));
    for(const auto& query:queries.array()) std::cout<<inspect(query.at("source").string()).dump()<<'\n';
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    try {return guarded(std::vector<wood::fs::path>(argv+1,argv+argc));}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
