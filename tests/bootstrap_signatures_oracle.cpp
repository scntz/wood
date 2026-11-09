#include "bootstrap_project_support.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <random>

namespace {
using namespace wood::bootstrap_test;
constexpr auto kinds=std::to_array<const char*>({"Unknown","Int","Float","Bool","Str","Void","List","Error","Dict","Named","Record","Json","Function","Stream","Database","Statement","Transaction","TlsContext","Task","Tuple","Optional","None","Parameter","Closure","Callable","Step","Cursor","Range","Projection","Signature","Array"});
static_assert(kinds.size()==wood::Type::Array+1);
struct Stage {
    wood::Compilation graph;
    wood::ModuleBindings bindings;
};
Stage load(const fs::path& input) {
    auto configured=configure(input);
    auto graph=wood::load_modules(configured.entry,configured.roots);
    auto bindings=wood::bind_modules(graph);
    try {
        std::vector<const wood::Program*> programs;
        for(std::size_t i=0;i<graph.modules.size();++i) {
            wood::resolve_signatures(graph.modules[i].program,bindings.environments[i]);
            programs.push_back(&graph.modules[i].program);
        }
        // Deliberately validates the UNSPECIALIZED declaration graph, not bodies.
        wood::validate_records(programs);
    } catch(wood::Diagnostic& error) {
        if(error.location.source<graph.modules.size()) {
            error.file=graph.modules[error.location.source].file;
            error.source_text=graph.modules[error.location.source].source;
        }
        throw;
    }
    return {std::move(graph),std::move(bindings)};
}
Json result(Json error,Json files=Json::Array{},Json types=Json::Array{},Json declarations=Json::Array{}) {
    return Json::Object{{"format",1},{"stage","declaration-signatures"},{"bodies_checked",false},
        {"lock_verified",false},{"layout_scope","unspecialized-declarations"},
        {"diagnostic",std::move(error)},{"files",std::move(files)},
        {"types",std::move(types)},{"declarations",std::move(declarations)}};
}
struct Writer {
    Json::Array types;
    std::map<std::string,Int> known;
    Int type(const wood::Type& value) {
        Int element=-1,key=-1,symbol=0;
        if(value.element) element=type(*value.element);
        if(value.key) key=type(*value.key);
        Json::Array parameters;
        for(const auto& item:value.parameters) parameters.emplace_back(type(item));
        auto name=value.name;
        if(value.kind==wood::Type::Record) {
            symbol=static_cast<Int>(value.declaration->symbol);
            name=value.declaration->name;
        }
        Json node=Json::Object{{"kind",kinds.at(value.kind)},{"name",name},{"element",element},
            {"key",key},{"parameters",std::move(parameters)},{"declaration",symbol},
            {"display",wood::type_name(value)}};
        const auto identity=node.dump();
        if(auto found=known.find(identity);found!=known.end()) return found->second;
        const auto index=static_cast<Int>(types.size());
        types.push_back(std::move(node)); known.emplace(identity,index); return index;
    }
};
Json defaults(const wood::Expr* expression) {
    Json::Array events;
    if(!expression) return events;
    std::vector<const wood::Expr*> pending{expression};
    while(!pending.empty()) {
        const auto* item=pending.back(); pending.pop_back();
        std::string kind;
        switch(item->kind) {
        case wood::Expr::Kind::Integer: kind="Integer";break;
        case wood::Expr::Kind::Float: kind="Float";break;
        case wood::Expr::Kind::String: kind="String";break;
        case wood::Expr::Kind::Bool: kind="Bool";break;
        case wood::Expr::Kind::None: kind="None";break;
        case wood::Expr::Kind::Unary: kind="Unary";break;
        case wood::Expr::Kind::Tuple: kind="Tuple";break;
        case wood::Expr::Kind::List: kind="List";break;
        case wood::Expr::Kind::Dict: kind="Dict";break;
        default: throw std::logic_error("unvalidated default reached signature wire");
        }
        events.emplace_back(Json::Object{{"kind",kind},{"text_hex",hex(item->text)},
            {"children",static_cast<Int>(item->children.size())},{"offset",static_cast<Int>(item->location.offset)},
            {"line",static_cast<Int>(item->location.line)},{"column",static_cast<Int>(item->location.column)}});
        for(auto it=item->children.rbegin();it!=item->children.rend();++it) pending.push_back(it->get());
    }
    return events;
}
Json describe(const Stage& stage) {
        Writer writer;
        Json::Array files,declarations;
        struct Decl {std::size_t module,root; Int method,parent; const wood::Stmt* stmt;};
        std::vector<Decl> items;
        for(std::size_t module=0;module<stage.graph.modules.size();++module) {
            const auto& unit=stage.graph.modules[module]; files.emplace_back(unit.file);
            for(std::size_t root=0;root<unit.program.statements.size();++root) {
                const auto* stmt=unit.program.statements[root].get();
                if(stmt->kind==wood::Stmt::Kind::Function || stmt->kind==wood::Stmt::Kind::Record)
                    items.push_back({module,root,-1,-1,stmt});
            }
        }
        const auto globals=items.size();
        for(std::size_t parent=0;parent<globals;++parent) {
            const auto item=items[parent];
            if(item.stmt->kind==wood::Stmt::Kind::Record)
                for(std::size_t method=0;method<item.stmt->body.size();++method)
                    items.push_back({item.module,item.root,static_cast<Int>(method),static_cast<Int>(parent),item.stmt->body[method].get()});
        }
        for(const auto& item:items) {
            const auto& stmt=*item.stmt;
            const auto return_type=writer.type(stmt.return_type);
            Json::Array parameters,type_parameters;
            for(std::size_t i=0;i<stmt.parameters.size();++i) {
                const auto& p=stmt.parameters[i];
                parameters.emplace_back(Json::Object{{"name",p.name},{"type",writer.type(p.type)},
                    {"mode",p.mode==wood::Parameter::Positional?"Positional":p.mode==wood::Parameter::PositionalOnly?"PositionalOnly":"KeywordOnly"},
                    {"mutable_borrow",p.mutable_borrow},{"field",stmt.kind==wood::Stmt::Kind::Record?static_cast<Int>(i):-1},
                    {"default_events",defaults(p.default_value.get())}});
            }
            for(const auto& name:stmt.type_parameters) type_parameters.emplace_back(name);
            declarations.emplace_back(Json::Object{{"module",static_cast<Int>(item.module)},{"root",static_cast<Int>(item.root)},
                {"method",item.method},{"symbol",static_cast<Int>(stmt.symbol)},{"parent",item.parent},
                {"kind",stmt.kind==wood::Stmt::Kind::Record?"Record":"Function"},{"name",stmt.name},{"result",return_type},
                {"parameters",std::move(parameters)},{"type_parameters",std::move(type_parameters)},
                {"external",stmt.external},{"mutating",stmt.mutating},{"inferred_return",stmt.inferred_return},
                {"offset",static_cast<Int>(stmt.location.offset)},{"line",static_cast<Int>(stmt.location.line)},
                {"column",static_cast<Int>(stmt.location.column)}});
        }
        return result(diagnostic(),std::move(files),std::move(writer.types),std::move(declarations));
}
Json inspect(const fs::path& input) {
    try { return describe(load(input)); }
    catch(const wood::Diagnostic& e) { return result(diagnostic(e.code,e.what(),e.file,e.location)); }
    catch(const std::exception& e) { return result(diagnostic("D5001",e.what(),wood::path_text(input))); }
}
int generate(const fs::path& directory) {
    fs::create_directories(directory);
    Json::Array entries;
    auto save=[&](const fs::path& file,std::string_view text) {fs::create_directories(file.parent_path());wood::write_file(file,text);};
    auto add=[&](const std::string& source) {
        auto path=directory/("case-"+std::to_string(entries.size()))/"main.wd";
        save(path,source); entries.emplace_back(wood::path_text(path));return path.parent_path();
    };
    for(const auto* text:{"", "fn f(x:int)->float:\n  return missing(x)\n", "record A:\n  x: int\nfn f(x:A)->A:\n  return x\n",
        "fn f[T](x:T)->T:\n  return x\n", "fn f[T](x:list[T],y:dict[T,str])->tuple[T,T | None]:\n  pass\n",
        "fn f[F](callback:F(int)->str)->F.Result:\n  pass\n", "fn f[T](source:T)->tuple[T.Item,T.Iterator]:\n  pass\n",
        "record Box[T]:\n  value: T\n  fn get(self)->T:\n    return self.value\n  fn set(mut self,value:T):\n    pass\n  fn map[U,F](self,f:F(T)->U)->Box[U]:\n    pass\n",
        "record Node:\n  next: list[Node]\n", "record Node:\n  next: Node | None\n", "record Node:\n  next: tuple[int,Node]\n",
        "record A:\n  value: B\nrecord B:\n  value: A\n", "record A:\n  value: list[B]\nrecord B:\n  value: A\n",
        "record A:\n  x:int\n  x:str\n", "fn f(x:int,x:str):\n  pass\n", "record A:\n  x:int\n  fn x(self):\n    pass\n",
        "record A:\n  fn x(self):\n    pass\n  fn x(self):\n    pass\n",
        "fn f[T,T](x:T):\n  pass\n", "record A[T]:\n  fn f[T](self,x:T):\n    pass\n",
        "fn f[T](x:T[int]):\n  pass\n", "fn f(x:Missing):\n  pass\n", "fn f(x:Missing[int]):\n  pass\n",
        "record A[T]:\n  x:T\nfn f(x:A):\n  pass\n", "record A:\n  x:int\nfn f(x:A[int]):\n  pass\n",
        "fn f(x:step[int],y:cursor[list[int]],z:array[float])->range:\n  pass\n", "fn f(x:step):\n  pass\n",
        "fn f(x:step[int,str]):\n  pass\n", "fn f(x:F(int)): return 0\n",
        "fn f(x:F(int)):\n  pass\n", "fn f[T](x:dict[T,int]):\n  pass\n",
        "record K:\n  x:int\nfn f(x:dict[K,int]):\n  pass\n",
        "record stream:\n  x:int\nfn f(x:stream)->stream:\n  return x\n",
        "fn f(x:stream,y:database,z:statement,a:transaction,b:tls_context,c:task):\n  pass\n",
        "fn f(x:None | None)->None:\n  pass\n", "fn f(x:int=1,y:float=-2.0,/,*,z:bool=True)->tuple[()]:\n  return ()\n",
        "fn f(x:list[int]=[1,~2],y:dict[str,int]={\"a\":3},z:tuple[int,bool]=(1,not False)):\n  pass\n",
        "fn f(x:int=1+2):\n  pass\n", "fn f(x:int=missing()):\n  pass\n", "fn f(x:int=True):\n  pass\n",
        "fn True():\n  pass\nfn f(x:bool=True):\n  pass\n", "record None:\n  x:int\nfn f(x:None | None=None):\n  pass\n",
        "extern fn native_sum(x:array[float])->float\n", "extern fn native_change(mut x:array[int])\n",
        "extern fn native_call(x:fn(int,float)->void,y:str)->int\n", "extern fn bad(x:bool)\n", "extern fn bad()->str\n",
        "extern fn bad(x:array[bool])\n", "extern fn bad(x:fn(str)->int)\n", "extern fn bad(x:callable(int)->int)\n",
        "extern fn _reserved(x:int)\n", "extern fn double(x:int)\n", "extern fn a__b(x:int)\n",
        "extern fn bad(mut x:array[int]=[])\n", "record Box[T]:\n  x:T\nfn f(x:Box[Box[int]]):\n  pass\n"}) add(text);
    for(const auto* name:{"int","float","str","dict","tuple","None","json","print","range","list","void","bool","error"})
        add("fn f["+std::string(name)+"](x:int):\n  pass\n");
    for(const auto* annotation:{"int","float","str","bool","json","error","stream","database","statement","transaction","tls_context","task","range","None","tuple[()]","tuple[int,str]","int | None","list[tuple[int,str]]","dict[str,list[int]]","fn(int)->str","callable(str)->void","array[int]","step[str]","cursor[list[int]]"})
        add("fn f(x:"+std::string(annotation)+")->"+annotation+":\n  pass\n");
    std::mt19937 random(0x5349474eU);
    for(int i=0;i<220;++i) {
        std::string type=i%3==0?"T":"int";
        for(int level=0;level<1+static_cast<int>(random()%8);++level) {
            switch(random()%6) {
            case 0:type="list["+type+"]";break;
            case 1:type="tuple["+type+",str]";break;
            case 2:type="("+type+") | None";break;
            case 3:type="dict[str,"+type+"]";break;
            case 4:type="fn("+type+")->int";break;
            default:type="Box["+type+"]";break;
            }
        }
        add("record Box[A]:\n  value:A\nfn f[T](x:"+type+")->"+type+":\n  pass\n");
    }
    for(int edges:{256,257}) for(bool reverse:{false,true}) {
        std::string text;
        for(int j=0;j<=edges;++j) {
            const int i=reverse?edges-j:j;
            text+="record R"+std::to_string(i)+":\n  value: "+(i==edges?std::string("int"):"R"+std::to_string(i+1))+"\n";
        }
        add(text);
    }
    for(int scenario=0;scenario<7;++scenario) {
        auto root=add("import lib\nfrom lib import Item as Alias\nfn f(x:lib.Item)->Alias:\n  pass\n");
        save(root/"lib.wd","record Item:\n  x:int\n");
        if(scenario==1) save(root/"lib.wd","record Item[T]:\n  x:T\n");
        if(scenario==2) save(root/"main.wd","import lib\nextern fn shared(x:float)\n");
        if(scenario==2) save(root/"lib.wd","extern fn shared(x:int)\n");
        if(scenario==3) {save(root/"main.wd","import lib\nextern fn shared(x:int)\n");save(root/"lib.wd","extern fn shared(x:int)\n");}
        if(scenario==4) {save(root/"main.wd","import lib\nrecord Item:\n  x:int\nfn f(a:Item,b:lib.Item):\n  pass\n");}
        if(scenario==5) {save(root/"main.wd","from lib import Item\nfn f[Item](x:int):\n  pass\n");}
        if(scenario==6) {save(root/"main.wd","import lib\nfn f(x:lib.Box[int])->lib.Box[int]:\n  pass\n");save(root/"lib.wd","record Box[T]:\n  value:T\n  fn get(self)->T:\n    return self.value\n");}
    }
    wood::write_file(directory/"entries.json",Json(entries).dump());
    std::cout<<entries.size()<<'\n';return 0;
}
int projections(const fs::path& input) {
    const auto queries=Json::parse(wood::read_file(input));
    for(const auto& query:queries.array()) {
        Stage stage;
        wood::ModuleUnit unit;
        unit.file="projection.wd";unit.source=query.at("source").string();
        unit.program=wood::parse(wood::lex(unit.source));
        stage.graph.modules.push_back(std::move(unit));
        try {
            stage.bindings=wood::bind_modules(stage.graph);
            auto& program=stage.graph.modules[0].program;
            wood::resolve_signatures(program,stage.bindings.environments[0]);
            auto& declaration=*program.statements.back();
            auto type=wood::Type::wrapped(wood::Type::Projection,declaration.parameters[0].type);
            type.name=query.at("member").string();
            declaration.return_type=type;
            wood::resolve_signatures(program,stage.bindings.environments[0]);
            std::cout<<describe(stage).dump()<<'\n';
        } catch(const wood::Diagnostic& e) {std::cout<<result(diagnostic(e.code,e.what(),"projection.wd",e.location)).dump()<<'\n';}
    }
    return 0;
}
int guarded(const std::vector<fs::path>& args) {
    try {
        if(args.size()==2 && args[0]=="--projections") return projections(args[1]);
        if(args.size()==2 && args[0]=="--generate") return generate(args[1]);
        if(args.size()==3 && args[0]=="--measure") {
            const int iterations=std::stoi(wood::path_text(args[1]));
            if(iterations<1 || iterations>10000) return 2;
            auto counts=[&]() {
                auto stage=load(args[2]);
                std::size_t declarations=0;
                for(const auto& module:stage.graph.modules) for(const auto& stmt:module.program.statements)
                    if(stmt->kind==wood::Stmt::Kind::Function || stmt->kind==wood::Stmt::Kind::Record) {
                        ++declarations;
                        if(stmt->kind==wood::Stmt::Kind::Record) declarations+=stmt->body.size();
                    }
                return std::pair(stage.graph.modules.size(),declarations);
            };
            (void)counts(); Int modules=0,symbols=0;
            const auto start=std::chrono::steady_clock::now();
            for(int i=0;i<iterations;++i) {auto [m,s]=counts();modules+=static_cast<Int>(m);symbols+=static_cast<Int>(s);}
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<Json(Json::Object{{"iterations",iterations},{"modules",modules},{"symbols",symbols},{"elapsed_ns",static_cast<Int>(elapsed)}}).dump()<<'\n';return 0;
        }
        if(args.empty()) return 2;
        int status=0;
        for(const auto& input:args) {auto output=inspect(input);if(!output.at("diagnostic").at("code").string().empty()) status=1;std::cout<<output.dump()<<'\n';}
        return status;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    return guarded(std::vector<fs::path>(argv+1,argv+argc));
}
