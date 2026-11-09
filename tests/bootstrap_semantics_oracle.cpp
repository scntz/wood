#include "bootstrap_project_support.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <random>

namespace {
using namespace wood::bootstrap_test;
constexpr auto expr_names=std::to_array<const char*>({"Integer","Float","String","Bool","Name","Unary","Binary","Call","Invoke","List","Dict","Index","Member","Slice","CompareChain","Conditional","ListComprehension","DictComprehension","Tuple","None","TypeApply","Closure"});
Json reply(Json error,Json events=Json::Array{},std::string result={}) {
    return Json::Object{{"diagnostic",std::move(error)},{"events",std::move(events)},{"return_type",std::move(result)}};
}
void project(const wood::Expr& root,Json::Array& events) {
    std::vector<const wood::Expr*> pending{&root};
    while(!pending.empty()) {
        const auto& node=*pending.back();pending.pop_back();
        const bool marker=node.kind==wood::Expr::Kind::Unary || node.kind==wood::Expr::Kind::Index || node.kind==wood::Expr::Kind::Member;
        events.emplace_back(Json::Object{{"kind",expr_names.at(static_cast<std::size_t>(node.kind))},
            {"text_hex",hex(node.text)},{"type",wood::type_name(node.type)},
            {"marker",marker?static_cast<Int>(node.symbol):0},{"offset",static_cast<Int>(node.location.offset)},
            {"line",static_cast<Int>(node.location.line)},{"column",static_cast<Int>(node.location.column)}});
        for(auto it=node.children.rbegin();it!=node.children.rend();++it) if(*it) pending.push_back(it->get());
    }
}
Json inspect(const Json& query) {
    try {
        wood::Compilation graph;
        wood::ModuleUnit unit;unit.file="query.wd";unit.source=query.at("source").string();
        unit.program=wood::parse(wood::lex(unit.source));graph.modules.push_back(std::move(unit));
        // The actual production analyzer supplies every type and diagnostic.
        // No test-side implementation of expression or flow rules is used.
        wood::analyze(graph);
        const auto mode=query.at("mode").string();
        const auto& program=graph.modules[0].program;
        Json::Array events;
        if(mode=="expression") project(*program.statements.back()->body.front()->expressions.front(),events);
        else if(mode=="defaults") {
            const auto defaults=[&](const wood::Stmt& stmt) {
                for(const auto& parameter:stmt.parameters)
                    if(parameter.default_value && parameter.default_value->type!=wood::Type::Unknown)
                        project(*parameter.default_value,events);
            };
            for(const auto& stmt:program.statements) if(stmt->kind==wood::Stmt::Kind::Function) defaults(*stmt);
            for(const auto& stmt:program.statements) if(stmt->kind==wood::Stmt::Kind::Record)
                for(const auto& method:stmt->body) defaults(*method);
        } else if(mode=="returns") return reply(diagnostic(),Json::Array{},wood::type_name(program.statements.back()->return_type));
        else if(mode!="flow") return reply(diagnostic("B3301","unknown semantic query mode"));
        return reply(diagnostic(),std::move(events));
    } catch(const wood::Diagnostic& error) {
        return reply(diagnostic(error.code,error.what(),"query.wd",error.location));
    }
}
Json corpus() {
    Json::Array entries;
    const auto add=[&](std::string mode,std::string source) {
        entries.emplace_back(Json::Object{{"mode",std::move(mode)},{"source",std::move(source)}});
    };
    const auto expression=[&](std::string value,std::string params="",std::string result="",std::string prelude="") {
        add("expression",prelude+"fn probe("+params+")"+(result.empty()?"":" -> "+result)+":\n    return "+value+"\n");
    };
    for(const auto* value:{"0","000123","9223372036854775807","9223372036854775808","99999999999999999999999999",
        "-9223372036854775808","-0009223372036854775808","+9223372036854775808","--9223372036854775808",
        "1.0","1e308","1e309","1e-400","0e99999","5e-324","2e-324","1.7976931348623157e308","1.7976931348623159e308",
        "true","false","True","False","None","'bytes á 🌳'","''","()","(1,)","(1,'a',true)","[1,2.5]","[2.5,1]","[True,1]","[]","{}",
        "{'a':1,'b':2.5}","{1.0:2}","{true:2}","{1:'a',true:'b'}","~1","~1.5","not true","not 1","-'a'",
        "[][0]","(1,'a')[-1]","(1,'a')[+0]","(1,'a')[-0]","(1,'a')[2]","()[0]","(1,'a')[-3]","(1,'a')[1+0]",
        "'abc'[::-1]","[1,2][1:]","[1,2][true:]","(1,2)[:]","1[0]","{'a':1}['a']","{'a':1}[1]","'abc'[false]",
        "[] == [1]","[1] == []","{} == {'a':1}","[] * 3","[] + [1]","1 < 2 < 3.0","1 < 2 < 'a'","1 in [1,2]",
        "[] in [[1]]","1 not in {'a':2}","'a' in 'abc'","1 is None","None is None","1 if true else 2.5","None if false else 2",
        "[] if true else [1]","[1] if false else []","1 if 1 else 2","1 if true else 'a'","[] if true else []"}) expression(value);
    for(const auto* value:{"1","1.5","true","'a'","[1]","(1,)","None"})
        for(const auto* other:{"2","2.5","false","'b'","[2]","(2,)","None"})
            for(const auto* op:{"+","-","*","/","//","%","&","|","^","<<",">>","==","!=","<","<=",">",">=","is","is not","in","not in","and","or"})
                expression(std::string(value)+" "+op+" "+other);
    for(const auto* hint:{"list[int]","list[float]","list[int | None]","list[list[int]]","dict[str,int]","dict[str,float]","dict[str,list[int]]","tuple[()]","tuple[int,str]","tuple[float,list[int]]","int | None","list[int] | None"})
        for(const auto* value:{"[]","[1]","[1,2.5]","[None,1]","[[]]","{}","{'a':1}","{'a':[]}","()","(1,'a')","(1,[])","None"}) expression(value,"",hint);
    for(const auto* type:{"int","float","bool","str","list[int]","dict[str,int]","tuple[int,str]","int | None","None","array[int]","array[float]","fn(int)->int","callable(int)->int","cursor[list[int]]","range","step[int]","error"}) {
        for(const auto* value:{"x","x + 1","x / 2","-x","x == x","x is None","x < x","x[0]","x[-1]","x[1:]","x.code","x.message","x.line","x.nope","None if true else x","x if false else None"}) expression(value,std::string("x:")+type);
    }
    for(const auto* value:{"item.value","item.flag","item.nope","item == item","item < item","item.value+1"})
        expression(value,"item:Item","","record Item:\n    value:int\n    flag:bool\n");
    for(const auto* type:{"int","float","callable(int)->int","list[int]"})
        for(const auto* value:{"item.value","item.value == item.value","item == item"})
            expression(value,"item:Box["+std::string(type)+"]","","record Box[T]:\n    value:T\n");
    expression("x[(1,2)]","x:array[float]");expression("x[(1,true)]","x:array[float]");
    expression("misspelled","misspelled_name:int");expression("vlaue","value:int");expression("valu","value:int,valve:int");
    for(const auto* type:{"int","float","bool","str","int | None","list[int]","list[float]","list[list[int]]","dict[str,int]","tuple[int,bool]","tuple[float,list[int]]"})
        for(const auto* value:{"0","1.5","True","None","'a'","[]","[1]","[[]]","{}","{'a':1}","(1,False)","(1,[])","-9223372036854775808","not 1","~1.5","1e999"})
            add("defaults","fn f(x:"+std::string(type)+"="+value+"):\n    pass\n");
    for(const auto* source:{"fn f[T](x:int='bad'):\n    pass\n", "record R[T]:\n    value:T\n    fn f(self,x:int='bad'):\n        pass\n",
        "record R:\n    value:int\n    fn f(self,x:list[float]=[1,2.5]):\n        pass\n", "record R:\n    value:int\n    fn f[T](self,x:int='bad'):\n        pass\n",
        "record R:\n    value:int\n    fn f(self,x:int='bad'):\n        pass\n",
        "fn f(x:bool=False,y:tuple[int,list[int]]=(1,[]),/,*,z:dict[str,list[float]]={'a':[]}):\n    pass\n"}) add("defaults",source);
    for(const auto* source:{"return\n","break\n","continue\n","raise 'a'\npass\n","exit(0)\npass\n",
        "fn f()->int:\n    pass\n", "fn f()->int:\n    while true:\n        return 1\n", "fn f()->int:\n    if true:\n        return 1\n",
        "fn f()->int:\n    if true:\n        return 1\n    else:\n        raise 'a'\n", "fn f():\n    return\n    pass\n",
        "fn f():\n    while true:\n        break\n        pass\n", "fn f():\n    while true:\n        continue\n        pass\n",
        "fn f():\n    for i in range(2):\n        break\n    pass\n", "fn f()->int:\n    try:\n        return 1\n    except err:\n        return 2\n",
        "fn f()->int:\n    try:\n        raise 'a'\n    except err:\n        pass\n", "fn f()->int:\n    try:\n        raise 'a'\n    except err:\n        return 2\n",
        "fn f():\n    while true:\n        fn inner():\n            break\n", "fn f[T](x:T)->int:\n    return 1\n    pass\n",
        "record R[T]:\n    value:T\n    fn f(self)->int:\n        pass\n", "record R:\n    value:int\n    fn f(self)->int:\n        pass\n",
        "fn f():\n    with stream_open('unused','r') as input:\n        return\n    pass\n",
        "fn f()->int:\n    with stream_open('unused','r') as input:\n        return 1\n",
        "fn f():\n    if true:\n        raise 'a'\n    else:\n        exit(0)\n    pass\n"}) add("flow",source);
    for(const auto* a:{"1","2.5","None","'a'","[1]","[2.5]","(1,'a')",""})
        for(const auto* b:{"1","2.5","None","'a'","[1]","[2.5]","(1,'a')",""})
            add("returns","fn f(flag:bool):\n    if flag:\n        return "+std::string(a)+"\n    else:\n        return "+b+"\n");
    for(const auto* source:{"fn f(x:int | None,flag:bool):\n    if flag:\n        return x\n    return 1\n",
        "fn f(flag:bool):\n    if flag:\n        return 1\n", "fn f(flag:bool):\n    if flag:\n        return None\n",
        "fn f():\n    raise 'a'\n", "fn f(flag:bool):\n    if flag:\n        return\n    return None\n"}) add("returns",source);
    std::string deep="fn f():\n";
    for(int i=1;i<=180;++i) deep+=std::string(static_cast<std::size_t>(i)*2,' ')+"if true:\n";
    deep+=std::string(362,' ')+"assert(";
    for(int i=0;i<100;++i) deep+="not ";
    deep+="true)\n";add("flow",deep);
    for(int closures:{63,64,65}) {
        std::string text="fn f():\n";
        for(int i=1;i<=closures;++i) text+=std::string(static_cast<std::size_t>(i)*2,' ')+"fn inner"+std::to_string(i)+"():\n";
        text+=std::string(static_cast<std::size_t>(closures+1)*2,' ')+"pass\n";add("flow",text);
    }
    return entries;
}
int guarded(const std::vector<fs::path>& arguments) {
    try {
        if(arguments.size()==2 && arguments[0]=="--generate") {
            const auto queries=corpus();wood::write_file(arguments[1],queries.dump());std::cout<<queries.array().size()<<'\n';return 0;
        }
        if(arguments.size()!=1 && arguments.size()!=3) return 2;
        const auto queries=Json::parse(wood::read_file(arguments[0]));
        int iterations=0;
        if(arguments.size()==3) {
            if(arguments[1]!="--measure") return 2;
            iterations=std::stoi(wood::path_text(arguments[2]));if(iterations<1 || iterations>10000) return 2;
        }
        for(const auto& query:queries.array()) {auto result=inspect(query);if(iterations==0) std::cout<<result.dump()<<'\n';}
        if(iterations) {
            Int checksum=0;
            const auto start=std::chrono::steady_clock::now();
            for(int i=0;i<iterations;++i) for(const auto& query:queries.array()) {
                const auto result=inspect(query);
                checksum+=static_cast<Int>(result.at("events").array().size()+result.at("diagnostic").at("code").string().size()+result.at("return_type").string().size());
            }
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<Json(Json::Object{{"iterations",iterations},{"queries",static_cast<Int>(queries.array().size())},{"checksum",checksum},{"elapsed_ns",static_cast<Int>(elapsed)}}).dump()<<'\n';
        }
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    return guarded(std::vector<fs::path>(argv+1,argv+argc));
}
