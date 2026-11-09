#include "wood/compiler.hpp"
#include "wood/support.hpp"
#include <array>
#include <iostream>
#include <functional>
#include <chrono>
#include <random>
#include <set>

namespace {
constexpr auto expr_names = std::to_array<const char*>({"Integer","Float","String","Bool","Name","Unary","Binary","Call","Invoke","List","Dict","Index","Member","Slice","CompareChain","Conditional","ListComprehension","DictComprehension","Tuple","None","TypeApply","Closure"});
constexpr auto stmt_names = std::to_array<const char*>({"Assign","IndexAssign","AugAssign","Expression","If","While","For","Return","Break","Continue","Pass","Function","Record","Import","Try","Raise","With","Finally"});
constexpr auto type_names = std::to_array<const char*>({"Unknown","Int","Float","Bool","Str","Void","List","Error","Dict","Named","Record","Json","Function","Stream","Database","Statement","Transaction","TlsContext","Task","Tuple","Optional","None","Parameter","Closure","Callable","Step","Cursor","Range","Projection","Signature","Array"});
static_assert(expr_names.size() == static_cast<std::size_t>(wood::Expr::Kind::Closure)+1);
static_assert(stmt_names.size() == static_cast<std::size_t>(wood::Stmt::Kind::Finally)+1);
static_assert(type_names.size() == wood::Type::Array+1);
std::string hex(std::string_view text) {
    const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(text.size()*2);
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result;
}
std::string flag(bool value) { return value ? "1" : "0"; }
struct Writer {
    wood::Json::Array events;
    void put(std::string tag, std::string value, wood::Location at = {0,0,0,0}) {
        events.emplace_back(wood::Json::Object{{"tag",std::move(tag)}, {"value_hex",hex(value)},
            {"offset",static_cast<std::int64_t>(at.offset)}, {"line",static_cast<std::int64_t>(at.line)},
            {"column",static_cast<std::int64_t>(at.column)}});
    }
    void strings(const char* tag, const std::vector<std::string>& values) {
        put(tag,std::to_string(values.size()));
        for (const auto& value : values) put("text",value);
    }
    void type(const wood::Type* value) {
        if (!value) { put("type","null"); return; }
        put("type",type_names.at(value->kind)); put("name",value->name);
        type(value->element.get()); type(value->key.get());
        put("type_parameters",std::to_string(value->parameters.size()));
        for (const auto& parameter : value->parameters) type(&parameter);
    }
    void expr(const wood::Expr* value) {
        if (!value) { put("expr","null"); return; }
        put("expr",expr_names.at(static_cast<std::size_t>(value->kind)),value->location);
        put("text",value->text); strings("keywords",value->keywords); strings("operators",value->operators);
        put("clauses",std::to_string(value->clauses.size()));
        for (const auto& clause : value->clauses) put("clause",clause.name,clause.location);
        put("children",std::to_string(value->children.size()));
        for (const auto& child : value->children) expr(child.get());
        put("type_arguments",std::to_string(value->type_arguments.size()));
        for (const auto& argument : value->type_arguments) type(&argument);
        stmt(value->function.get());
    }
    void block(const char* tag, const wood::Block& values) {
        put(tag,std::to_string(values.size()));
        for (const auto& value : values) stmt(value.get());
    }
    void stmt(const wood::Stmt* value) {
        if (!value) { put("stmt","null"); return; }
        put("stmt",stmt_names.at(static_cast<std::size_t>(value->kind)),value->location);
        put("name",value->name); put("module",value->module); put("relative",std::to_string(value->relative));
        put("flags",flag(value->external)+flag(value->method)+flag(value->mutating)+flag(value->inferred_return)+flag(value->expression_body));
        strings("type_parameters",value->type_parameters); strings("mutable_captures",value->mutable_captures);
        put("imports",std::to_string(value->imports.size()));
        for (const auto& name : value->imports) { put("import",name.name,name.location); put("alias",name.alias); }
        type(&value->return_type); type(&value->annotation);
        put("parameters",std::to_string(value->parameters.size()));
        for (const auto& parameter : value->parameters) {
            put("parameter",parameter.name,parameter.location);
            put("mode",parameter.mode == wood::Parameter::Positional ? "Positional" : parameter.mode == wood::Parameter::PositionalOnly ? "PositionalOnly" : "KeywordOnly");
            put("mutable_borrow",flag(parameter.mutable_borrow)); type(&parameter.type); expr(parameter.default_value.get());
        }
        put("expressions",std::to_string(value->expressions.size()));
        for (const auto& expression : value->expressions) expr(expression.get());
        block("body",value->body); block("otherwise",value->otherwise);
    }
};
wood::Json scan(std::string_view source) {
    wood::Location at;
    std::string code, message;
    Writer writer;
    try {
        const auto tokens = wood::lex(source);
        auto program = wood::parse(tokens);
        at = tokens.back().location;
        writer.block("program",program.statements);
    } catch (const wood::Diagnostic& error) {
        at = error.location; code = error.code; message = error.what(); writer.events.clear();
    }
    return wood::Json::Object{{"format",1},{"code",code},{"message",message},
        {"offset",static_cast<std::int64_t>(at.offset)}, {"line",static_cast<std::int64_t>(at.line)},
        {"column",static_cast<std::int64_t>(at.column)}, {"events",std::move(writer.events)}};
}
int generate(const wood::fs::path& directory) {
    wood::fs::create_directories(directory);
    int count = 0;
    std::set<std::string> exprs, stmts, errors;
    auto save = [&](std::string source) {
        wood::write_file(directory / (std::to_string(count++)+".wd"),source);
        auto result = scan(source);
        if (!result.at("code").string().empty()) errors.insert(result.at("code").string());
        for (const auto& event : result.at("events").array()) {
            const auto tag = event.at("tag").string();
            if (tag == "expr") exprs.insert(event.at("value_hex").string());
            if (tag == "stmt") stmts.insert(event.at("value_hex").string());
        }
    };
    for (const auto& source : {"", "# empty", "x=1+2*3<<4&5^6|7\ny=not a and b or c\nz=~x//2%3-+4\n",
        "x=(1,2.0,'a',true,false,None)\na=[]\nb={}\nc=()\nd=(1,)\n",
        "x=a.b.c\ny=a.b.c(1,y=2)\nz=(f)(2)\nw=f(1)(2).x[0]\n",
        "x=a[1:2:3]\ny=a[:]\nz=a[::]\nw=a[1,2,3]\nv=a[1,]\n",
        "x=[i for i in xs if i>0 for j in ys if j != 0]\ny={i:j for i in xs for j in ys}\n",
        "x=a<b<=c==d!=e>f>=g in h not in i is j is not None\ny=a if b else c if d else e\n",
        "record Box[T]:\n  value: T\n  fn set(mut self,x: T):\n    self.value=x\n  def get(self)->T:\n    return self.value\nrecord Empty:\n  pass\n",
        "import std.io as io\nfrom ..pkg.mod import a as b,c\n",
        "fn identity[T](x:T,/,y:int=1,*,z:str='x')->T:\n  return x\nextern fn scale(mut x:array[float],n:int)\n",
        "fn outer(x:int):\n  fn inner[mut x](y:int):\n    x+=y\n    return x\n  f=fn[mut x](y:int)->int=>x+y\n  return inner\n",
        "x=Box[list[int]]([1])\ny=callbacks[0](1)\nz=a[b+c](2)\nw=a[B,C](2)\nv=a[fn(int)->int](2)\n",
        "x:tuple[()]=()\ny:tuple[int,list[str],dict[str,int],int|None]=(1,[],{},None)\nf:callable(int)->void=g\np:F(int)->str=g\nq:(int|None)|None=None\n",
        "if a:\n  pass\nelif b:\n  break\nelse:\n  continue\nwhile a:\n  a-=1\nfor x in xs:\n  print(x)\n",
        "try:\n  with io.open('x','r') as input:\n    data=io.read_all(input)\nexcept err:\n  raise err\n",
        "try:\n  x=1\nexcept err:\n  raise\nfinally:\n  pass\ntry:\n  pass\nfinally:\n  raise\nfinally=1\nfinally:int=2\n",
        "a[0]=1\na.field=2\na[0]+=3\nreturn\nraise 'x'\n",
        "fn f():\n  if true:\n    return 1\nfn g():\n  fn h():\n    return 1\n  return\n",
        "with=1\nrecord=2\ndef=3\nwith(1)\nTrue=4\nFalse=5\n",
        "x=\n", "if true\n  pass\n", "fn f():\npass\n", "x: void=1\n", "x:int|str=1\n",
        "x:dict[float,int]={}\n", "x=1+not true\n", "x=1 if a\n", "x=a not b\n",
        "x=f(a=1,2)\n", "fn f(x:int=1,y:int):\n  pass\n", "fn f(*):\n  pass\n",
        "fn f(/):\n  pass\n", "fn f(x:int,/,/):\n  pass\n", "fn f(*,*,x:int):\n  pass\n",
        "1=2\n", "if true:\n  import a\n", "if true:\n  record X:\n    pass\n",
        "fn f():\n  extern fn g()\n", "record X:\n  fn bad(x):\n    pass\n",
        "fn f[T]():\n  fn g[U]():\n    pass\n", "extern fn f[T]()\n",
        "fn f(mut x:int):\n  pass\n", "f=fn[x]()=>1\n", "fn f[mut x]():\n  pass\n",
        "x=[i for 1 in xs]\n", "from a import b,\n", "try:\n  pass\n",
        "x=a[1:2:3:4]\n", "fn f(x int):\n  pass\n", "x:tuple[]=()\n",
        "x:fn(int) g\n", "x:int|None|None=1\n", "x=(1 2)\n", "x=[1 2]\n", "x={1 2}\n"}) save(source);
    // Boundary pairs, not only rejection: preserve every current parser limit.
    for (int n : {255,256,257}) {
        std::string args, parameters, types, captures, clauses, chain;
        for (int i=0; i<n; ++i) {
            args += "1,"; parameters += "p"+std::to_string(i)+":int,"; types += "int,";
            captures += "mut x,"; clauses += " if true"; chain += "<x";
        }
        save("x=f("+args+")\n"); save("fn f("+parameters+"):\n  pass\n");
        save("x=("+args+")\n"); save("x:tuple["+types+"]=()\n");
        save("x:fn("+types+")->int=f\n"); save("x:F("+types+")->int=f\n");
        save("x=x"+chain+"\n");
        save("x="+std::string(static_cast<std::size_t>(n),'-')+"1\n");
        save("x="+std::string(static_cast<std::size_t>(n),'(')+"1"+std::string(static_cast<std::size_t>(n),')')+"\n");
        std::string elifs = "if true:\n  pass\n";
        for (int i=1;i<n;++i) elifs += "elif true:\n  pass\n";
        save(elifs);
    }
    for (int n : {32,33,64,65}) {
        std::string names, types, clauses, fields, nested="int";
        for (int i=0;i<n;++i) {
            names += "T"+std::to_string(i)+","; types += "int,"; clauses += " if true";
            fields += "  p"+std::to_string(i)+":int\n"; nested = "list["+nested+"]";
        }
        save("record X["+names+"]:\n  pass\n"); save("fn f["+names+"]():\n  pass\n");
        save("x:X["+types+"]=None\n"); save("x=X["+types+"]()\n");
        save("x: "+nested+"=[]\n"); save("x=[i for i in xs"+clauses+"]\n");
    }
    for (int n : {8,9,255,256}) {
        std::string fields, axes;
        for (int i=0;i<n;++i) { fields += "  p"+std::to_string(i)+":int\n"; axes += "1,"; }
        save("record X:\n"+fields); save("x=a["+axes+"]\n");
    }
    for (int n : {255,256}) {
        std::string nested, parameters;
        for (int i=0;i<n;++i) {
            nested.append(static_cast<std::size_t>(i),' '); nested += "while true:\n";
            parameters += "p"+std::to_string(i)+":int,";
        }
        nested.append(static_cast<std::size_t>(n),' '); nested += "pass\n";
        save(nested); save("x=fn("+parameters+")=>1\n");
    }
    for (const std::string source : {"x='a\\0b'\ny='á🌳'\n", "fn f(x:(int|None)|None)->json:\n  raise 'x'\n", "x:a.b.C[error]=None\n"}) {
        save(source); save("\xef\xbb\xbf"+source);
        auto crlf=source;
        for (std::size_t at=0; (at=crlf.find('\n',at))!=std::string::npos; at+=2) crlf.replace(at,1,"\r\n");
        save(crlf); save(source.substr(0,source.size()-1));
    }
    const std::string seed = "fn f[T](x:T,y:int=1)->T:\n  if y>0:\n    return x\n  return x\na=f[int](1,y=2)\n";
    // Deletion/insertion mutations reach parser errors much more often than raw bytes.
    for (std::size_t i=0;i<seed.size();++i) { auto source=seed; source.erase(i,1); save(source); }
    std::mt19937 random(0x50415253);
    for (int trial=0;trial<256;++trial) {
        auto source=seed;
        source.insert(random()%source.size(),std::string(1,"(),:[]+-*/=abcdefghijklmnopqrstuvwxyz\n "[random()%38]));
        save(source);
    }
    std::function<std::string(int)> expression = [&](int depth) -> std::string {
        if (depth==0) {
            const std::array<std::string,8> leaves{"1","2e3","'x'","true","None","a.b","f()","()"};
            return leaves[random()%leaves.size()];
        }
        const auto left=expression(depth-1), right=expression(depth-1);
        switch (random()%9) {
        case 0: return "("+left+" + "+right+")";
        case 1: return "("+left+" < "+right+" is not None)";
        case 2: return "("+left+" if "+right+" else None)";
        case 3: return "["+left+", "+right+"]";
        case 4: return "{"+left+":"+right+"}";
        case 5: return "f("+left+",key="+right+")";
        case 6: return "("+left+", "+right+")";
        case 7: return "("+left+")[1:2]";
        default: return "["+left+" for x in "+right+" if true]";
        }
    };
    for (int trial=0;trial<256;++trial) {
        auto source="value = "+expression(3)+"\n";
        if (!scan(source).at("code").string().empty())
            throw std::runtime_error("grammar generator produced invalid syntax");
        save(source);
    }
    // The parser does not emit Expr::None: it intentionally preserves contextual names.
    for (const auto* kind : expr_names)
        if (std::string_view(kind)!="None" && !exprs.contains(hex(kind)))
            throw std::runtime_error(std::string("missing expression coverage: ")+kind);
    for (const auto* kind : stmt_names)
        if (!stmts.contains(hex(kind))) throw std::runtime_error(std::string("missing statement coverage: ")+kind);
    for (const char* code : {"E2001","E2002","E2003","E2004","E2006","E2010","E2011","E2012","E3020","E3023","E3027","E3028"})
        if (!errors.contains(code)) throw std::runtime_error(std::string("missing diagnostic coverage: ")+code);
    std::cout << count << '\n';
    return 0;
}
int guarded(const std::vector<wood::fs::path>& arguments) {
    try {
        if (arguments.size()==3 && arguments[0]=="--measure") {
            const auto count=std::stoi(wood::path_text(arguments[1]));
            if (count<1 || count>10000) throw std::runtime_error("iterations must be between 1 and 10000");
            const auto source=wood::read_file(arguments[2]);
            const auto roots = [&]() { return static_cast<std::int64_t>(wood::parse(wood::lex(source)).statements.size()); };
            (void)roots();
            std::int64_t checksum=0;
            const auto started=std::chrono::steady_clock::now();
            for (int i=0;i<count;++i) checksum+=roots();
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
            std::cout << wood::Json(wood::Json::Object{{"iterations",count},{"source_bytes",static_cast<std::int64_t>(source.size())},
                {"roots",checksum},{"elapsed_ns",static_cast<std::int64_t>(elapsed)}}).dump() << '\n';
            return 0;
        }
        if (arguments.size()==2 && arguments[0]=="--contract") {
            const auto contract=wood::Json::parse(wood::read_file(arguments[1]));
            if (contract.at("format").integer()!=1) return 1;
            auto check = [&](const char* key, const auto& names) {
                const auto& values=contract.at(key).array();
                if (values.size()!=names.size()) throw std::runtime_error(std::string("wrong contract size: ")+key);
                for (std::size_t i=0;i<values.size();++i)
                    if (values[i].string()!=names[i]) throw std::runtime_error(std::string("wrong contract name: ")+key);
            };
            check("expressions",expr_names); check("statements",stmt_names); check("types",type_names);
            return 0;
        }
        if (arguments.size()==2 && arguments[0]=="--generate") return generate(arguments[1]);
        if (arguments.empty()) return 2;
        int status=0;
        for (const auto& path : arguments) {
            auto result=scan(wood::read_file(path));
            if (!result.at("code").string().empty()) status=1;
            std::cout << result.dump() << '\n';
        }
        return status;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    return guarded(std::vector<wood::fs::path>(argv+1,argv+argc));
}
