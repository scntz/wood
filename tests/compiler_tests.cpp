#include "wood/compiler.hpp"
#include "wood/support.hpp"
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
bool tracing = false;
void check(bool condition, const std::string& name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
void accepts(const std::string& source) {
    if (tracing)
        std::cerr << "ACCEPT " << checks << " " << source.substr(0, 100) << std::endl;
    try {
        check(!wood::compile(source, "test.wd").empty(), source);
    } catch (const std::exception& error) {
        check(false, source + "\n" + error.what());
    }
}
void rejects(const std::string& source, const std::string& code) {
    if (tracing)
        std::cerr << "REJECT " << checks << " " << source.substr(0, 100) << std::endl;
    try {
        (void)wood::compile(source, "test.wd");
        check(false, "accepted invalid input: " + source);
    } catch (const wood::Diagnostic& error) {
        check(error.code == code, source + " expected " + code + ", got " + error.code);
    } catch (const std::exception& error) {
        check(false, "unexpected compiler exception: " + std::string(error.what()));
    }
}
void diagnoses(const std::string& source, const std::string& code, const std::string& message,
               std::size_t line, std::size_t column) {
    try {
        (void)wood::compile(source, "test.wd");
        check(false, "accepted diagnostic regression: " + source);
    } catch (const wood::Diagnostic& error) {
        check(error.code == code, "diagnostic code: " + source);
        check(error.what() == message, "diagnostic message: " + source + "\n" + error.what());
        std::size_t offset = 0;
        for (std::size_t i = 1; i < line; ++i)
            offset = source.find('\n', offset) + 1;
        offset += column - 1;
        check(error.location.line == line && error.location.column == column &&
                  error.location.offset == offset,
              "precise diagnostic location: " + source);
        const wood::Json expected(wood::Json::Object{{"severity", "error"},
                                                     {"code", code},
                                                     {"file", "test.wd"},
                                                     {"line", static_cast<std::int64_t>(line)},
                                                     {"column", static_cast<std::int64_t>(column)},
                                                     {"message", message}});
        check(wood::Json::parse(wood::render_diagnostic(error, "test.wd", source, true)) ==
                  expected,
              "unchanged six-field diagnostic JSON: " + source);
        const auto human = wood::render_diagnostic(error, "test.wd", source);
        check(human.find("test.wd:" + std::to_string(line) + ":" + std::to_string(column) +
                         ": error[" + code + "]: " + message + "\n") == 0 &&
                  human.find(std::string(column + 1, ' ') + "^\n") != std::string::npos,
              "human message and caret: " + source);
    } catch (const std::exception& error) {
        check(false, "unexpected diagnostic regression exception: " + std::string(error.what()));
    }
}
void smarter_diagnostics() {
    diagnoses("total = 1\nprint(totla)\n", "E3007",
              "undefined variable 'totla'; did you mean 'total'?", 2, 7);
    diagnoses("message = 'ok'\nprint(mesage)\n", "E3007",
              "undefined variable 'mesage'; did you mean 'message'?", 2, 7);
    diagnoses("message = 'ok'\nprint(messsage)\n", "E3007",
              "undefined variable 'messsage'; did you mean 'message'?", 2, 7);
    diagnoses("total = 1\nprint(totxl)\n", "E3007",
              "undefined variable 'totxl'; did you mean 'total'?", 2, 7);
    diagnoses("total = 1\nprint(Total)\n", "E3007",
              "undefined variable 'Total'; did you mean 'total'?", 2, 7);
    diagnoses("prnit('ok')\n", "E3005", "unknown function 'prnit'; did you mean 'print'?", 1, 1);
    diagnoses("fn encode(value: int) -> int:\n    return value\nencdoe(1)\n", "E3005",
              "unknown function 'encdoe'; did you mean 'encode'?", 3, 1);
    diagnoses("fn encode(value: int) -> int:\n    return value\ncallback = encdoe\n", "E3007",
              "undefined variable 'encdoe'; did you mean 'encode'?", 3, 12);
    diagnoses("record Point:\n    x: int\nPiont(1)\n", "E3005",
              "unknown function 'Piont'; did you mean 'Point'?", 3, 1);
    diagnoses("runner = fn(value: int) => value\nrunenr(1)\n", "E3005",
              "unknown function 'runenr'; did you mean 'runner'?", 2, 1);
    diagnoses("fn outer(total: int):\n    fn inner():\n        return totla\n    return inner\n",
              "E3007", "undefined variable 'totla'; did you mean 'total'?", 3, 16);
    // No guess when ambiguous, distant, short, inaccessible or not a callable.
    diagnoses("count = 1\ncourt = 2\nprint(coust)\n", "E3007", "undefined variable 'coust'", 3, 7);
    diagnoses("total = 1\nprint(tozax)\n", "E3007", "undefined variable 'tozax'", 2, 7);
    diagnoses("at = 1\nprint(an)\n", "E3007", "undefined variable 'an'", 2, 7);
    diagnoses("print(totla)\ntotal = 1\n", "E3007", "undefined variable 'totla'", 1, 7);
    diagnoses("if true:\n    total = 1\nprint(totla)\n", "E3007", "undefined variable 'totla'", 3,
              7);
    diagnoses("total = 1\nfn isolated() -> int:\n    return totla\n", "E3007",
              "undefined variable 'totla'", 3, 12);
    diagnoses("runner = 1\nrunenr()\n", "E3005", "unknown function 'runenr'", 2, 1);
    diagnoses("record Point:\n    x: int\nvalue = Piont\n", "E3007", "undefined variable 'Piont'",
              3, 9);
    const std::string long_name(65, 'a');
    diagnoses(long_name + " = 1\nprint(" + long_name + "a)\n", "E3007",
              "undefined variable '" + long_name + "a'", 2, 7);
    const std::string function =
        "fn paint(color: str, count: int = 1, *, vivid: bool):\n    pass\n";
    const std::string expected = " (expected paint(color: str, count: int = ..., *, vivid: bool); ";
    diagnoses(function + "paint()\n", "E3003",
              "missing argument 'color' to 'paint'" + expected + "provided 0 arguments)", 3, 1);
    diagnoses(function + "paint('red')\n", "E3003",
              "missing argument 'vivid' to 'paint'" + expected + "provided 1 argument)", 3, 1);
    diagnoses(function + "paint('red',1,true)\n", "E3003",
              "too many positional arguments to 'paint'" + expected + "provided 3 arguments)", 3,
              15);
    diagnoses(function + "paint('red', vivid=true, shade=1)\n", "E3003",
              "unknown keyword 'shade' to 'paint'" + expected + "provided 3 arguments)", 3, 32);
    diagnoses(function + "paint('red', color='blue')\n", "E3003",
              "multiple values for parameter 'color' in 'paint'" + expected +
                  "provided 2 arguments)",
              3, 20);
    diagnoses("fn take(value: int, /):\n    pass\ntake(value=1)\n", "E3003",
              "parameter 'value' is positional-only in 'take' (expected take(value: int, /); "
              "provided 1 argument)",
              3, 12);
    diagnoses("record Point:\n    x: int\nPoint()\n", "E3003",
              "missing argument 'x' to 'Point' (expected Point(x: int); provided 0 arguments)", 3,
              1);
    diagnoses("record Point:\n    x: int\n    fn shift(self, amount: int):\n        "
              "pass\nPoint(1).shift()\n",
              "E3003",
              "missing argument 'amount' to 'shift' (expected shift(amount: int); provided "
              "0 arguments)",
              5, 15);
    diagnoses("len()\n", "E3003", "wrong number of arguments to 'len' (expected 1, got 0)", 1, 1);
    diagnoses("range(1,2,3,4)\n", "E3003",
              "wrong number of arguments to 'range' (expected 1..3, got 4)", 1, 1);
    diagnoses("fn take(value: int):\n    pass\ncallback = take\ncallback()\n", "E3003",
              "wrong number of arguments to function value (expected 1, got 0)", 4, 1);
    diagnoses("fn take(value: int):\n    pass\ntake('bad')\n", "E3002",
              "expected int, got str for argument 'value' to 'take'", 3, 6);
    diagnoses("fn take(value: int):\n    pass\ntake(value='bad')\n", "E3002",
              "expected int, got str for argument 'value' to 'take'", 3, 12);
    diagnoses("fn take(value: int):\n    pass\ncallback = take\ncallback('bad')\n", "E3002",
              "expected int, got str for argument 1 to function value", 4, 10);
    diagnoses("value: int = 'wrong'\n", "E3002", "expected int, got str", 1, 14);
    diagnoses("value = 1\nvalue = 'wrong'\n", "E3002", "expected int, got str", 2, 9);
    diagnoses("values = [1]\nvalues[0] = 'wrong'\n", "E3002", "expected int, got str", 2, 13);
    diagnoses("fn take() -> int:\n    return 'wrong'\n", "E3002", "expected int, got str", 2, 12);
    diagnoses("fn take(value: int = 'wrong'):\n    pass\n", "E3002", "expected int, got str", 1,
              22);
    diagnoses("if 1:\n    pass\n", "E3002", "expected bool, got int", 1, 4);
    diagnoses("while 1:\n    pass\n", "E3002", "expected bool, got int", 1, 7);
    diagnoses("value: list[int] = ['wrong']\n", "E3002", "expected int, got str", 1, 21);
    diagnoses("# UTF-8: \xc3\xa1\r\nvalue: int = 'wrong'\r\n", "E3002", "expected int, got str", 2,
              14);
    accepts("total = 1\nassert(total == 1)\n");
    accepts("fn take(value: float = 1, *, flag: bool = true):\n    pass\ntake()\ntake(flag=false, "
            "value=2)\n");
    accepts("record Point:\n    x: int\n    fn shift(self, amount: float = 1):\n        pass\n"
            "Point(1).shift()\nPoint(1).shift(amount=2)\n");
    accepts("fn take(value: int) -> int:\n    return value\ncallback = take\nassert(callback(1) == "
            "1)\n");
    accepts("fn outer(total: int):\n    fn inner():\n        return total\n    return inner\n"
            "assert(outer(2)() == 2)\n");
}
} // namespace
int main(int argc, char** argv) {
    tracing = argc > 1 && std::string_view(argv[1]) == "--trace";
    smarter_diagnostics();
    accepts("a = array_full((2,3),0.0)\na[1,2] = 3.0\nassert(a[1,2] == 3.0)\n");
    rejects("array_missing(array_full(3,0.0))\n", "E3005");
    rejects("array_matmul(array_full((2,2),1),array_full((2,2),1))\n", "E3031");
    rejects("array_affine(array_full((2,3),1),array_full((4,4),1))\n", "E3031");
    accepts(
        "a: array[int] = array_from_list([1,2])\na[0] += 2\nfor value in a:\n    print(value)\n");
    accepts("extern fn scale(mut values: array[float], factor: float)\na = "
            "array_full(3,1.0)\nscale(a,2.0)\n");
    accepts(
        "extern fn sum(values: array[int]) -> int\na = array_full(3,1)\nsum(a)\nf = sum\nf(a)\n");
    accepts("record array[T]:\n    item: T\na: array[str] = array('wood')\n");
    rejects("a: array[str] = []\n", "E3031");
    rejects("array_full((2,3),true)\n", "E3031");
    rejects("array_full((2,'x'),0.0)\n", "E3031");
    rejects("array_full((),0.0)\n", "E3031");
    rejects("array_full(3,0.0)[0.5]\n", "E3031");
    rejects("array_full((2,3),0.0)[0,'x']\n", "E3031");
    rejects("a = array_full(3,1)\na[0] = 2.0\n", "E3002");
    rejects("array_full(3,0.0) + 'x'\n", "E3031");
    rejects("array_full(3,0.0) < array_full(3,0.0)\n", "E3002");
    rejects("json_dump(array_full(3,0.0))\n", "E3022");
    rejects("extern fn scale(mut value: int)\n", "E3023");
    rejects("extern fn scale(mut value: array[float] = [])\n", "E3023");
    rejects("extern fn scale(mut value: array[float])\nscale(array_full(3,1.0))\n", "E3015");
    rejects("extern fn scale(mut value: array[float])\nf = scale\n", "E3023");
    rejects("fn scale(mut value: array[float]):\n    pass\n", "E3023");
    rejects("extern fn scale(value: array[float])\nextern fn scale(mut value: array[float])\n",
            "E3001");
    accepts("fn f(x: int):\n    return x + 1\nassert(f(1) == 2)\n");
    accepts("fn f[T](x: T):\n    return x\nf(1)\nf('x')\n");
    accepts("fn make(x: int):\n    return fn(y: int) => x + y\nmake(1)(2)\n");
    accepts("fn f():\n    fn local():\n        return 1\n    local()\nf()\n");
    accepts("fn f(x: bool):\n    if x:\n        return 1\n    return 2.5\nf(true)\n");
    accepts("fn f(x: bool):\n    if x:\n        return None\n    return 1\nf(true)\n");
    accepts("fn f(x: bool):\n    if x:\n        return 2.5\n    return 1\nf(true)\n");
    accepts("fn f(x: bool):\n    if x:\n        return 1\n    raise 'missing'\nf(true)\n");
    accepts("fn a(x: int):\n    return b(x)\nfn b(x: int):\n    return x\na(1)\n");
    accepts("fn a(x: int) -> int:\n    return b(x)\nfn b(x: int):\n    return a(x)\n");
    accepts("fn f():\n    return None\nf()\n");
    accepts("record Box[T]:\n    value: T\n    fn map[F](self, f: F(T)):\n        return "
            "f(self.value)\nBox(1).map(fn(x) => x+1)\n");
    rejects("fn f():\n    return f()\n", "E3030");
    rejects("fn a():\n    return b()\nfn b():\n    return a()\n", "E3030");
    rejects("fn f[T](x: T):\n    return f(x)\nf(1)\n", "E3030");
    rejects("fn f(x: bool):\n    if x:\n        return 1\n    return 'x'\n", "E3030");
    rejects("fn f(x: bool):\n    if x:\n        return 1\n    return\n", "E3030");
    rejects("fn f(x: bool):\n    if x:\n        return None\n    return\n", "E3030");
    rejects("fn f(x: bool):\n    if x:\n        return 1\n", "E3013");
    rejects("fn f():\n    return []\n", "E3014");
    rejects("fn f(x: bool):\n    if x:\n        return fn() => 1\n    return fn() => 2\n", "E3030");
    rejects("fn f(x: bool, y: int | None):\n    if x:\n        return y\n    return 2.5\n",
            "E3030");
    std::string return_chain;
    for (int i = 0; i < 65; ++i)
        return_chain += "fn infer" + std::to_string(i) + "():\n    return infer" +
                        std::to_string(i + 1) + "()\n";
    return_chain += "fn infer65():\n    return 1\n";
    rejects(return_chain, "E2002");
    std::string bounded_return_chain;
    for (int i = 0; i < 63; ++i)
        bounded_return_chain += "fn bounded" + std::to_string(i) + "():\n    return bounded" +
                                std::to_string(i + 1) + "()\n";
    bounded_return_chain += "fn bounded63():\n    return 1\n";
    accepts(bounded_return_chain);
    accepts("fn id[T](x: T):\n    return x\nf: fn(int) -> int = id\nf(1)\n");
    accepts("fn f():\n    return g\nfn g(x: int):\n    return x\nf()(1)\n");
    accepts("fn f(x: bool):\n    y: int | None = None\n    if x:\n        return y\n    return "
            "2\nf(true)\n");
    accepts("fn f(x: bool):\n    if x:\n        return None\n    return None\nf(true)\n");
    accepts("fn f():\n    result: list[int] = []\n    return result\nf()\n");
    accepts("fn f():\n    return print('native')\nf()\n");
    rejects("fn f(x: bool):\n    if x:\n        return [1]\n    return [2.0]\n", "E3030");
    rejects("fn f() -> int:\n    return 'x'\n", "E3002");
    rejects("fn f():\n    while true:\n        return 1\n", "E3013");
    rejects("record R:\n    fn value(self):\n        return self.value()\nR().value()\n", "E3030");
    auto item_projection = wood::Type::wrapped(wood::Type::Projection, wood::Type::parameter("T"));
    item_projection.name = "Item";
    auto iterator_projection = item_projection;
    iterator_projection.name = "Iterator";
    check(item_projection != iterator_projection && item_projection == item_projection,
          "associated type identity includes its member name");
    // RHS comprehensions grow the scope vector. On MSVC this can copy its
    // unordered_maps; assignments must not retain pointers into the old maps.
    for (int depth = 1; depth <= 10; ++depth) {
        std::string rhs = "['payload',str(i)]";
        std::string initial = "['payload','0']";
        for (int level = 0; level < depth; ++level) {
            rhs = "[" + rhs + " for i in range(1)]";
            initial = "[" + initial + "]";
        }
        accepts("value = " + initial + "\nvalue = " + rhs + "\n");
        rejects("value = 1\nvalue = " + rhs + "\n", "E3002");
    }
    for (int depth = 1; depth <= 12; ++depth) {
        std::string rhs = "str(i)", initial = "'old'", annotation = "str";
        for (int level = 0; level < depth; ++level) {
            rhs = "[" + rhs + " for i in range(1) if i == 0]";
            initial = "[" + initial + "]";
            annotation = "list[" + annotation + "]";
        }
        accepts("value = " + initial + "\nif true:\n    value = " + rhs + "\n");
        accepts("fn f(value: " + annotation + "):\n    value = " + rhs +
                "\nf(" + initial + ")\n");
        accepts("fn f[T](value: T) -> T:\n    value = " + rhs +
                "\n    return value\nf(" + initial + ")\n");
        accepts("fn outer():\n    value = " + initial +
                "\n    fn update[mut value]():\n        value = " + rhs +
                "\n    update()\nouter()\n");
        accepts("table = {'key':" + initial + "}\ntable = {str(j):" + rhs +
                " for j in range(1)}\n");
        accepts("value = (" + initial + ", 7)\nvalue = (" + rhs + ", 8)\n");
        rejects("value = " + initial + "\nvalue = " + rhs + "\nprint(i)\n", "E3007");
    }
    rejects("fn outer():\n    values = [['old']]\n    fn change():\n"
            "        values = [[str(i)] for i in range(1)]\n    change()\nouter()\n",
            "E3028");
    rejects("fn outer():\n    values = [['old']]\n    fn change[mut values]():\n"
            "        values = [i for i in range(1)]\n    change()\nouter()\n", "E3002");
    const auto iteration_call = "wd_read_iteration(wd_t";
    check(wood::compile("items=['long value']\nfor item in items:\n    print(len(item))\n",
                        "test.wd").find(iteration_call) != std::string::npos,
          "readonly string loop reads its owned snapshot without an element copy");
    check(wood::compile("items=['long value']\nx=[len(item) for item in items]\n", "test.wd")
                  .find(iteration_call) != std::string::npos,
          "readonly string comprehension uses a scoped snapshot read");
    check(wood::compile("for item in ['long value']:\n    if true:\n        item+='!'\n",
                        "test.wd").find(iteration_call) == std::string::npos,
          "nested writes keep an owned string loop binding");
    check(wood::compile("for item in ['long value']:\n    fn change[mut item]():\n"
                        "        item+='!'\n    change()\n", "test.wd")
                  .find(iteration_call) == std::string::npos,
          "mutable captures conservatively keep owned loop bindings");
    accepts("base = 3\nf = fn(x: int) => x + base\nassert(f(2) == 5)\n");
    accepts("f: callable(int) -> int = fn(x) => x + 1\nf(2)\n");
    accepts("record step[T]:\n    value: T\nx: step[int] = step(1)\n");
    accepts("record cursor[T]:\n    value: T\nx: cursor[int] = cursor(1)\n");
    accepts("fn id[T](x: T) -> T:\n    return x\nf: callable(int) -> int = id\nf(2)\n");
    rejects("fn f(x: cursor[int]):\n    pass\n", "E3029");
    rejects("record R[F]:\n    items: list[F.Result]\nx: R[fn() -> void] = R([])\n", "E3002");
    rejects("record R[F]:\n    item: F.Result\nx: R[fn() -> void] = R(None)\n", "E3002");
    accepts("fn make(n: int) -> callable() -> int:\n    fn counter[mut n]() -> int:\n        n += "
            "1\n        return n\n    return counter\nx = make(0)\nx()\n");
    accepts("if true:\n    fn f():\n        pass\n    f()\nx = range(3)\nfor i in x:\n    pass\n");
    accepts("fn apply[T,F](call: F(T) -> T, x: T) -> T:\n    return call(x)\nassert(apply(fn(x) => "
            "x + 1, 2) == 3)\n");
    accepts(
        "record Numbers:\n    value: int\n    fn next(mut self) -> step[int]:\n        self.value "
        "+= 1\n        return yielded(self.value)\nc = Numbers(0)\nfor x in c:\n    break\n");
    rejects("f = fn(x) => x\n", "E3028");
    rejects("x = 1\nfn f() -> int:\n    return x\nf()\n", "E3007");
    rejects("x = 1\nif true:\n    fn f() -> int:\n        x += 1\n        return x\n    f()\n",
            "E3028");
    rejects("items = [1]\nf = fn() => append(items,2)\n", "E3028");
    rejects("x = 1\nif true:\n    fn f():\n        x = 2\n", "E3028");
    rejects("f = fn [mut missing]() => 1\n", "E3028");
    rejects("x = 1\nf = fn [mut x,mut x]() => x\n", "E3028");
    rejects("x = 1\nf = fn [mut x](x: int) => x\n", "E3028");
    rejects("f: fn(int) -> int = fn(x: int) => x\n", "E3002");
    rejects("f: callable(str) -> int = fn(x: int) => x\n", "E3002");
    rejects("f = fn(x: int) => x\nf()\n", "E3003");
    rejects("f = fn(x: int) => x\nf(y=1)\n", "E3003");
    rejects("f = fn(x: int) => x\nprint(f == f)\n", "E3028");
    rejects("f = fn(x: int) => x\nprint(f in [f])\n", "E3028");
    rejects("f = fn(x: int) => x\njson_dump(f)\n", "E3022");
    rejects("x = finished()\n", "E3029");
    rejects("step_value(3)\n", "E3029");
    rejects("next(iter([1]))\n", "E3015");
    rejects("record Bad:\n    fn next(self) -> step[int]:\n        return finished()\nfor i in "
            "Bad():\n    pass\n",
            "E3029");
    rejects(
        "record Bad:\n    fn next(mut self) -> int:\n        return 1\nfor i in Bad():\n    pass\n",
        "E3029");
    rejects("record Bad:\n    fn iter(mut self) -> cursor[list[int]]:\n        return "
            "iter([1])\nfor i in Bad():\n    pass\n",
            "E3029");
    rejects("fn bad[F](f: F(int) -> bool):\n    pass\nbad(fn(x: int) => x)\n", "E3002");
    rejects("fn f(x: F(int)):\n    pass\n", "E3028");
    rejects("fn f[T](x: T.Item):\n    pass\nf(1)\n", "E3024");
    rejects("if true:\n    fn f(x: int = len('a')):\n        pass\n", "E3026");
    rejects("record R:\n    call: callable() -> int\n    fn get(self) -> int:\n        return "
            "self.call()\n",
            "E3027");
    accepts("record R:\n    call: callable() -> int\n    fn get(mut self) -> int:\n        return "
            "self.call()\n");
    std::string capture_source, capture_items;
    for (int i = 0; i < 256; ++i) {
        capture_source += "v" + std::to_string(i) + " = 1\n";
        if (i)
            capture_items += ",";
        capture_items += "v" + std::to_string(i);
        if (i == 254)
            accepts(capture_source + "callback = fn() => [" + capture_items + "]\n");
    }
    rejects(capture_source + "callback = fn() => [" + capture_items + "]\n", "E2002");
    std::string nested_closure = "0";
    for (int i = 0; i < 64; ++i)
        nested_closure = "fn() => " + nested_closure;
    accepts("f = " + nested_closure + "\n");
    rejects("f = fn() => " + nested_closure + "\n", "E2002");
    std::string capture_chain = "c0 = fn() => 0\n";
    for (int i = 1; i <= 256; ++i)
        capture_chain += "c" + std::to_string(i) + " = fn() => c" + std::to_string(i - 1) + "()\n";
    accepts(capture_chain);
    rejects(capture_chain + "last = fn() => c256()\n", "E2002");
    std::string nominal_leaf = "Leaf";
    for (int i = 0; i < 64; ++i)
        nominal_leaf = "list[" + nominal_leaf + "]";
    accepts("record Leaf:\n    value: int\nfn unchanged(value: " + nominal_leaf + "):\n    pass\n");
    rejects("record Leaf:\n    value: int\nfn too_deep(value: list[" + nominal_leaf +
                "]):\n    pass\n",
            "E2002");
    const std::string box =
        "record Box[T]:\n    value: T\n    fn get(self) -> T:\n        return self.value\n    fn "
        "set(mut self, value: T):\n        self.value = value\n";
    accepts(box + "b = Box(1)\nb.set(2)\nassert(b.get() == 2)\n");
    accepts(box + "b = Box[float](1)\nb.set(2)\n");
    accepts(box + "b: Box[list[int]] = Box([])\nappend(b.value, 2)\n");
    accepts(box + "fn get[T](b: Box[T]) -> T:\n    return b.get()\nassert(get(Box('x')) == 'x')\n");
    accepts(box + "fn wrap[T](x: T) -> Box[T]:\n    return Box[T](x)\nprint(wrap(1))\n");
    accepts("record Empty[T]:\n    pass\nx = Empty[int]()\ny: Empty[str] = Empty()\n");
    accepts("record Node[T]:\n    value: T\n    children: list[Node[T]]\nn = Node(1, "
            "[])\nappend(n.children, Node(2, []))\n");
    accepts(
        "record Pair[A,B]:\n    first: A\n    second: B\np = Pair[int,str](second='ok',first=4)\n");
    accepts("record Counter:\n    value: int\n    fn add(mut self, amount: int = 1, *, twice: bool "
            "= false) -> int:\n        self.value += amount * (2 if twice else 1)\n        return "
            "self.value\nc = Counter(1)\nc.add(twice=true)\n");
    accepts("fn choose(x: int) -> int:\n    return x\nvalues = [choose]\nindex = "
            "0\nassert(values[index](2) == 2)\n");
    rejects(box + "x: Box = Box(1)\n", "E3024");
    rejects(box + "x: Box[int,str] = Box(1)\n", "E3024");
    rejects(box + "x = Box[int,str](1)\n", "E3024");
    rejects(box + "x = Box[int]('x')\n", "E3002");
    rejects(box + "x = Box(1)\nx.set('x')\n", "E3002");
    rejects(box + "x = Box(1)\nx = Box('x')\n", "E3002");
    rejects(box + "x: Box[float] = Box[int](1)\n", "E3002");
    rejects(box + "x = Box(1).set(2)\n", "E3015");
    rejects(box + "x = Box(1)\nf = x.get\n", "E3016");
    rejects(box + "x = Box(1)\nx.get(self=x)\n", "E3003");
    rejects("record Bad[T,T]:\n    x: T\n", "E3024");
    rejects("record Bad[int]:\n    x: int\n", "E3024");
    rejects("record Bad[T]:\n    x: Missing\n", "E2003");
    rejects("record Bad[T]:\n    x: T\n    fn bad[T](self, x: T):\n        pass\n", "E3024");
    rejects("record Bad:\n    x: int\n    fn x(self):\n        pass\n", "E3001");
    rejects("record Bad:\n    fn same(self):\n        pass\n    fn same(self):\n        pass\n",
            "E3001");
    rejects("record Bad:\n    fn bad(other):\n        pass\n", "E3027");
    rejects("record Bad:\n    x: int\n    fn bad(self):\n        self.x = 1\n", "E3027");
    rejects("record Bad:\n    x: int\n    fn bad(self):\n        self.x += 1\n", "E3027");
    rejects("record Bad:\n    items: list[int]\n    fn bad(self):\n        append(self.items, 1)\n",
            "E3027");
    rejects("record Bad:\n    items: list[int]\n    fn bad(self):\n        self.items[0] = 1\n",
            "E3027");
    rejects("record Bad:\n    x: int\n    fn bad(self):\n        self = Bad(1)\n", "E3027");
    rejects("record Bad:\n    x: int\n    fn change(mut self):\n        self.x = 1\n    fn "
            "bad(self):\n        self.change()\n",
            "E3027");
    rejects("record Bad:\n    fn bad(self) -> int:\n        pass\n", "E3013");
    rejects("record Cycle[T]:\n    child: Cycle[T]\nx: Cycle[int] = Cycle[int](None)\n", "E3002");
    rejects("record Cycle[T]:\n    child: Cycle[T] | None\nx = Cycle[int](None)\n", "E3021");
    rejects("record Grow[T]:\n    child: list[Grow[list[T]]]\nx = Grow[int]([])\n", "E2002");
    rejects("record Keys[K]:\n    items: dict[K,int]\nx = Keys[float]({})\n", "E3020");
    rejects("x: stream[int] = None\n", "E3024");
    accepts(
        box +
        "record Outer[T]:\n    inner: Box[T]\nx = Outer(Box('x'))\nassert(x.inner.get() == 'x')\n");
    accepts(box + "b = Box[fn(int) -> int](strange)\nfn strange(x: int) -> int:\n    return "
                  "x\nassert(b.value(1) == 1)\n");
    rejects("record Bad[T]:\n    fn call(self, x: int, x: int):\n        pass\n", "E3001");
    rejects(box + "x: Box[Missing] = Box(1)\n", "E2003");
    rejects("record Maybe[T]:\n    value: T | None\nx = Maybe(None)\n", "E3024");
    accepts("record Maybe[T]:\n    value: T | None\nx: Maybe[int] = Maybe(None)\n");
    rejects("record Cycle[T]:\n    child: Cycle[T]\nfn unused(x: Cycle[int]):\n    pass\n",
            "E3021");
    std::string method_chain = "record Chain[T]:\n    value: T\n";
    for (int i = 0; i < 300; ++i) {
        method_chain += "    fn step" + std::to_string(i) + "(self) -> T:\n        return ";
        method_chain += i == 299 ? "self.value\n" : "self.step" + std::to_string(i + 1) + "()\n";
    }
    accepts(method_chain + "assert(Chain(1).step0() == 1)\n");
    auto cached_records =
        wood::parse(wood::lex(box + "a = Box(1)\nb = Box(2)\nc = Box('x')\na.get()\nb.get()\n"));
    wood::analyze(cached_records);
    check(cached_records.statements[0]->specializations.size() == 2,
          "record specializations are cached by nominal type arguments");
    check(cached_records.statements[0]->specializations[0]->body[0]->checked,
          "concrete method is checked once and emitted");
    std::string record_instances = "record Tag[T]:\n    pass\n";
    for (int i = 0; i < 1024; ++i)
        record_instances += "record Type" + std::to_string(i) + ":\n    pass\nTag[Type" +
                            std::to_string(i) + "]()\n";
    accepts(record_instances);
    rejects(record_instances + "Tag[int]()\n", "E2002");
    accepts("record Optional[T]:\n    value: T | None\nx = Optional[None](None)\nassert(x.value is "
            "None)\n");
    rejects(box + "fn bad(x: Box[int]) -> Box[str]:\n    return x\n", "E3002");
    rejects(box + "fn bad[T](x: Box[T]) -> T:\n    return 'bad'\nbad(Box(1))\n", "E3002");
    rejects("record Bad[T]:\n    value: T\n    fn change(self):\n        self.value = "
            "self.value\nBad(1).change()\n",
            "E3027");
    accepts("record Empty[T]:\n    fn identity(self, value: T) -> T:\n        return "
            "value\nassert(Empty[int]().identity(2) == 2)\n");
    std::string optional_depth = "int";
    for (int i = 0; i < 32; ++i)
        optional_depth = "list[" + optional_depth + "] | None";
    accepts("x: " + optional_depth + " = None\n");
    accepts("def deep(x: " + optional_depth + ") -> " + optional_depth + ":\n    return x\n");
    optional_depth = "list[" + optional_depth + "] | None";
    rejects("x: " + optional_depth + " = None\n", "E2002");
    rejects("def deep(x: " + optional_depth + "):\n    pass\n", "E2002");
    rejects("def deep() -> " + optional_depth + ":\n    return None\n", "E2002");
    rejects("record Deep:\n    value: " + optional_depth + "\n", "E2002");
    std::string generic_chain;
    for (int i = 0; i < 65; ++i) {
        generic_chain += "def layer" + std::to_string(i) + "[T](item: T) -> T:\n    return ";
        generic_chain += i == 64 ? "item\n" : "layer" + std::to_string(i + 1) + "(item)\n";
    }
    accepts(generic_chain + "assert(layer1(5) == 5)\n");
    rejects(generic_chain + "layer0(5)\n", "E2002");
    rejects("def grow[T](item: T) -> int:\n    return grow([item])\ngrow(1)\n", "E2002");
    std::string instances = "def same_type[T](item: T) -> T:\n    return item\n";
    for (int i = 0; i < 1024; ++i)
        instances += "record Value" + std::to_string(i) + ":\n    number: int\nsame_type(Value" +
                     std::to_string(i) + "(1))\n";
    accepts(instances);
    rejects(instances + "record Extra:\n    number: int\nsame_type(Extra(1))\n", "E2002");
    accepts("x = ()\ny = (1,)\nz: tuple[int,str] = (1,'x')\nprint(z[-1], len(x))\n");
    accepts("x: int | None = None\nx = 3\nprint(unwrap(x), x is not None, None is None)\n");
    accepts("def pick[T](x: T) -> T:\n    return x\nx = pick(1)\ny = pick('x')\n");
    accepts("def pick[T](x: T) -> T:\n    return x\nx = pick(None)\nassert(x is None)\n");
    accepts("def pick[T](x: T) -> T:\n    return x\nx: fn(int) -> int = pick\nprint(x(3))\n");
    accepts("def get[T](x: T | None, default: T) -> T:\n    return default if x is None else "
            "unwrap(x)\nprint(get(None, 1))\n");
    accepts("def call(a: int, /, b: int = 2, *, c: int) -> int:\n    return "
            "a+b+c\nprint(call(1,c=4))\n");
    accepts("record A:\n    x: tuple[int, B | None]\nrecord B:\n    y: int\na = A((1,None))\n");
    rejects("x: int | str = 1\n", "E2003");
    rejects("x: void | None = None\n", "E2003");
    rejects("x: int | None = None\ny: int = x\n", "E3002");
    rejects("x: int | None = 'x'\n", "E3002");
    rejects("print(unwrap(1))\n", "E3002");
    rejects("print(None < None)\n", "E3002");
    rejects("print(1 is 1)\n", "E3002");
    rejects("x = (1, 'a')\ni = 0\nprint(x[i])\n", "E3025");
    rejects("print((1,)[1])\n", "E3025");
    rejects("print((1,)[-2])\n", "E3025");
    rejects("print(()[0])\n", "E3025");
    rejects("x = (1,)\nx[0] = 2\n", "E3015");
    rejects("x: tuple[int,str] = (1,)\n", "E3002");
    rejects("x = (print(1),)\n", "E3002");
    rejects("record A:\n    x: tuple[int, A | None]\n", "E3021");
    rejects("def bad[T,T](x: T) -> T:\n    return x\n", "E3024");
    rejects("def bad[int](x: int) -> int:\n    return x\n", "E3024");
    rejects("def same[T](x: T, y: T) -> T:\n    return x\nsame(1,'x')\n", "E3024");
    rejects("def pick[T](x: T) -> T:\n    return x\nx = pick\n", "E3024");
    rejects("def bad[T](x: T) -> T:\n    return x + 'x'\nbad(1)\n", "E3002");
    rejects("def make[T]() -> T:\n    raise 'no'\nmake()\n", "E3024");
    rejects("def bad(x: int = 1, y: int) -> int:\n    return x\n", "E2001");
    rejects("def bad(x: int = len('x')) -> int:\n    return x\n", "E3026");
    rejects("def bad(x: int = 'x') -> int:\n    return x\n", "E3002");
    rejects("def f(x: int) -> int:\n    return x\nf(x=1,x=2)\n", "E3003");
    rejects("def f(x: int) -> int:\n    return x\nf(1,x=2)\n", "E3003");
    rejects("def f(x: int) -> int:\n    return x\nf(y=1)\n", "E3003");
    rejects("def f(x: int) -> int:\n    return x\nf(x=1,2)\n", "E2001");
    rejects("def f(x: int, /) -> int:\n    return x\nf(x=1)\n", "E3003");
    rejects("def f(*, x: int) -> int:\n    return x\nf(1)\n", "E3003");
    rejects("def f(*, x: int) -> int:\n    return x\nf()\n", "E3003");
    rejects("def f(x: int = 1) -> int:\n    return x\nf()\nfv = f\nfv()\n", "E3003");
    rejects("def f(x: int) -> int:\n    return x\nfv = f\nfv(x=1)\n", "E3003");
    rejects("print(value=1)\n", "E3003");
    rejects("for x in range(stop=1):\n    pass\n", "E3003");
    rejects("def f(/, x: int):\n    pass\n", "E2001");
    rejects("def f(x: int, *,):\n    pass\n", "E2001");
    accepts("print(1 < 2 < 3, 1 < 2 in [2], 3 not in [1] == [1])\n");
    accepts("x = 1 if true else 2.5\ny: list[int] = [] if false else [1]\n");
    accepts("s = 'abcdef'[::-1]\nx = [1,2,3][1::2]\ny = x[:]\n");
    accepts("x = [i * i for i in range(5) if i % 2 == 0]\n");
    accepts("x = [[a + b for b in range(a)] for a in range(3)]\n");
    accepts("x = {str(i): i for i in range(5) if i > 0 for j in [i]}\n");
    accepts("x: list[list[int]] = [[] for i in range(3)]\n");
    accepts("x: dict[str,list[int]] = {str(i): [] for i in range(3)}\n");
    accepts("x: list[float] = [1 + i for i in range(3)]\n");
    accepts("x = [1,2] + [3]\ny = [1] * 3\nz = 3 * 'ab'\n");
    accepts("x: list[int] = [] * 3\ny: list[int] = [] + []\n");
    accepts("x = 2\nx += 1\nx -= 1\nx *= 2\nx //= 2\nx %= 2\nx <<= 1\nx >>= 1\nx &= 3\nx |= 1\nx "
            "^= 2\n");
    accepts("x = [1,2]\nx[0] += pop(x)\ny = {'a': [1]}\ny['a'] += [2]\n");
    accepts("def twice(x: int) -> int:\n    return x * 2\nassert(True and not False)\n");
    accepts("fn def(x: int) -> int:\n    return x\nTrue = 7\nFalse = 8\nassert(def(True) == 7)\n");
    rejects("x += 1\n", "E3007");
    rejects("x = 1\nx /= 2\n", "E3002");
    rejects("[1][0] += 1\n", "E3015");
    rejects("x = [1]\nx[:] += [2]\n", "E3015");
    rejects("x = [1]\nx[:] = [2]\n", "E2010");
    rejects("x = 'x'[::1.0]\n", "E3002");
    rejects("x = {1:2}[:]\n", "E3002");
    rejects("x = [1 if true]\n", "E2001");
    rejects("x = 1 if 2 else 3\n", "E3002");
    rejects("x = 1 if true else 'text'\n", "E3002");
    rejects("x = print(1) if true else print(2)\n", "E3002");
    rejects("x = (1 < 2) < 3\n", "E3002");
    rejects("x = 1 < 2 < 'text'\n", "E3002");
    rejects("x = [1] + [2.5]\n", "E3002");
    rejects("x = [1] * 2.5\n", "E3002");
    rejects("x = [i for i in range(3)]\nprint(i)\n", "E3007");
    rejects("x = [i for i in 1]\n", "E3010");
    rejects("x = [i for i in range(3) if i]\n", "E3002");
    rejects("x = [print(i) for i in range(3)]\n", "E3002");
    rejects("x = {[i]: i for i in range(3)}\n", "E3020");
    rejects("x = [i for i in []]\n", "E3014");
    rejects("x = [i for i in range(3),]\n", "E2001");
    rejects("x = [i for i in range(3) if true else false]\n", "E2001");
    rejects("x = [i for print in range(3)]\n", "E3001");
    std::string clauses = "x = [0";
    for (int i = 0; i < 65; ++i)
        clauses += " for x" + std::to_string(i) + " in range(1)";
    rejects(clauses + "]\n", "E2002");
    accepts(clauses.substr(0, clauses.rfind(" for x64")) + "]\n");
    std::string chain = "print(0";
    for (int i = 0; i < 256; ++i)
        chain += " == 0";
    rejects(chain + ")\n", "E2002");
    accepts(chain.substr(0, chain.size() - 5) + ")\n");
    accepts("fn True() -> int:\n    return 1\nfn False() -> int:\n    return 0\nassert(True() == 1 "
            "and False() == 0)\n");
    accepts("with db_open(':memory:', 1000) as db:\n    with db_prepare(db, 'SELECT 1') as "
            "query:\n        stmt_step(query)\n");
    accepts("fn work(value: json) -> json:\n    return value\nwith task_spawn(work, json(1)) as "
            "job:\n    task_wait(job, 1000)\n");
    accepts("record Native:\n    db: database\n    query: statement\n    tx: transaction\n    tls: "
            "tls_context\n    job: task\n");
    accepts("record database:\n    id: int\nx: database = database(1)\n");
    rejects("with db_open(':memory:', 1) as db:\n    pass\nprint(db)\n", "E3007");
    rejects("db_prepare(1, 'SELECT 1')\n", "E3002");
    rejects("db_open(':memory:', 'x')\n", "E3002");
    rejects("stmt_bind_text(db_open(':memory:', 1), 1, 'x')\n", "E3002");
    rejects("json(db_open(':memory:', 1))\n", "E3022");
    rejects("db = db_open(':memory:', 1)\nprint(db < db)\n", "E3002");
    rejects("fn work(value: int) -> int:\n    return value\ntask_spawn(work, json(1))\n", "E3002");
    rejects("extern fn f(x: database)\n", "E3023");
    accepts("fn db_open(x: int) -> int:\n    return x\nassert(db_open(1) == 1)\n");
    accepts("with stream_open('file', 'r') as file:\n    stream_read(file, 1)\n");
    accepts("fn f(source: stream) -> stream:\n    with source as file:\n        return file\n");
    accepts("with = 1\nprint(with)\n");
    accepts("fn with(x: int) -> int:\n    return x\nprint(with(2))\n");
    accepts("record stream:\n    value: int\nx: stream = stream(1)\n");
    accepts("fn stream() -> int:\n    return 1\nfn identity(x: stream) -> stream:\n    return x\n");
    accepts("x = stream_open('x', 'r')\ny: list[stream] = [x]\nprint(y)\n");
    rejects("with 1 as file:\n    pass\n", "E3002");
    rejects("with stream_open('x', 'r') as file:\n    pass\nprint(file)\n", "E3007");
    rejects("with stream_open('x', 'r') as print:\n    pass\n", "E3001");
    rejects("stream_read(1, 2)\n", "E3002");
    rejects("stream_read(stream_open('x', 'r'), 'x')\n", "E3002");
    rejects("stream_open('x')\n", "E3003");
    rejects("tcp_connect('host', '80', 100)\n", "E3002");
    rejects("stream_close(stream_open('x', 'r'), 1)\n", "E3003");
    rejects("x = stream_open('x', 'r')\njson_dump([x])\n", "E3022");
    rejects("record File:\n    value: stream\nx = File(stream_open('x', 'r'))\njson(x)\n", "E3022");
    rejects("x = stream_open('x', 'r')\nprint(x < x)\n", "E3002");
    rejects("extern fn read(file: stream) -> int\n", "E3023");
    for (const auto& text : std::vector<std::string>{
             "",
             "# comment\n",
             "print('hello')",
             "print()\n",
             "x = 0009\nprint(x)\n",
             "x: float = 1\nx = 2.5\n",
             "print(1 + 2 * 3, not 1 == 2, true and false or true)\n",
             "print(1.25e-2, 1e3, '\\n\\r\\t\\0\\\\\\\"\\\'')\n",
             "fn twice(x: int) -> int:\n    return x * 2\nprint(twice(21))\n",
             "fn even(n: int) -> bool:\n    if n == 0:\n        return true\n    return odd(n - "
             "1)\nfn odd(n: int) -> bool:\n    if n == 0:\n        return false\n    return even(n "
             "- 1)\n",
             "fn f(n: int) -> int:\n    if n > 0:\n        return 1\n    elif n == 0:\n        "
             "return 0\n    else:\n        return -1\n",
             "fn f():\n    return\nf()\n",
             "fn f() -> void:\n    pass\n",
             "if true:\n    # empty comment\n\n    pass\nelse:\n    pass\n",
             "x = 0\nwhile x < 10:\n    x = x + 1\n    if x == 2:\n        continue\n    if x == "
             "5:\n        break\n",
             "for i in range(10):\n    print(i)\nfor i in range(10, 0, -1):\n    pass\n",
             "print(\n  1,\n  2, # comment\n)\n",
             "x = -9223372036854775808\n",
             "print('a' + 'b', len('abc'), str(42), int(-1.5), float(2))\nassert(true, 'ok')\n",
             "\xef\xbb\xbf"
             "x = 1\r\nif true:\r\n    print(x)\r\n",
             "x = 1\nif true:\n    x = 2\nprint(x)\n",
             "print(not not true, 0e-999, 1.7976931348623157e308, 4.9406564584124654e-324)\n",
             "fn f(a: float,) -> float:\n    return a\nprint(f(1,))\n",
             "fn f(a: int) -> int:\n    while a > 0:\n        if a == 2:\n            return 5\n   "
             "     a = a - 1\n    return 0\n"})
        accepts(text);
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"\tprint(1)\n", "E1001"},
        {"x = 1\t\n", "E1001"},
        {"if true:\n    pass\n  pass\n", "E1002"},
        {"x = 1.\n", "E1003"},
        {"x = 1e+\n", "E1003"},
        {"x = 123abc\n", "E1003"},
        {"print(1e-999)\n", "E3006"},
        {"print(true == not false)\n", "E2004"},
        {"print('unterminated)\n", "E1004"},
        {"print('\\q')\n", "E1005"},
        {"print(1\n", "E1006"},
        {"print(1))\n", "E1006"},
        {"x = @\n", "E1007"},
        {"if true:\npass\n", "E2001"},
        {"x = 1 x = 2\n", "E2001"},
        {"x: vector = 1\n", "E2003"},
        {"x: void = 1\n", "E2003"},
        {"x =\n", "E2004"},
        {"print = 1\n", "E3001"},
        {"fn print():\n    pass\n", "E3001"},
        {"fn f(a: int, a: int):\n    pass\n", "E3001"},
        {"fn f():\n    pass\nfn f():\n    pass\n", "E3001"},
        {"x = 1\nx = 'oops'\n", "E3002"},
        {"if 1:\n    pass\n", "E3002"},
        {"print(true + 1)\n", "E3002"},
        {"print(1.0 // 2)\n", "E3002"},
        {"print(1.0 % 2)\n", "E3002"},
        {"print(1 and true)\n", "E3002"},
        {"print(not 1)\n", "E3002"},
        {"print(-'x')\n", "E3002"},
        {"print(1 == '1')\n", "E3002"},
        {"print(true < false)\n", "E3002"},
        {"print(len(2))\n", "E3002"},
        {"print(int(true))\n", "E3002"},
        {"assert(1)\n", "E3002"},
        {"x = print(1)\n", "E3002"},
        {"print(print(1))\n", "E3002"},
        {"fn f(n: int):\n    pass\nf('x')\n", "E3002"},
        {"fn f() -> int:\n    return\n", "E3002"},
        {"print(len())\n", "E3003"},
        {"assert()\n", "E3003"},
        {"for i in range():\n    pass\n", "E3003"},
        {"unknown()\n", "E3005"},
        {"print(9223372036854775808)\n", "E3006"},
        {"print(-9223372036854775809)\n", "E3006"},
        {"print(1e999)\n", "E3006"},
        {"print(x)\n", "E3007"},
        {"x = x + 1\n", "E3007"},
        {"if true:\n    x = 1\nprint(x)\n", "E3007"},
        {"x = 1\nfn f() -> int:\n    return x\n", "E3007"},
        {"for i in range(2):\n    pass\nprint(i)\n", "E3007"},
        {"fn f():\n    return\n    print(1)\n", "E3008"},
        {"x = 1\nx: int = 2\n", "E3009"},
        {"for i in 5:\n    pass\n", "E3010"},
        {"return 1\n", "E3011"},
        {"break\n", "E3012"},
        {"continue\n", "E3012"},
        {"fn f() -> int:\n    pass\n", "E3013"},
        {"fn f(n: int) -> int:\n    if n > 0:\n        return n\n", "E3013"}};
    for (const auto& [source, code] : invalid)
        rejects(source, code);
    for (const auto& source : std::vector<std::string>{
             "a = [1, 2, 3]\na[0] = 4\nappend(a, pop(a))\n",
             "a: list[list[float]] = [[], [1, 2]]\na[1][0] = 2\n",
             "fn empty() -> list[int]:\n    return []\nassert(empty() == [])\n",
             "fn f(a: list[int]) -> int:\n    return len(a)\nf([])\n",
             "for i in [1, 2]:\n    print(i)\nfor c in 'ab':\n    print(c)\n",
             "print([\n    1,\n    2,\n][-1])\n",
             "print(int('+2'), float('-1.2e3'), split('a,b', ','), join([], ','))\n",
             "print(args(), read_line(), eof(), strip(' x '), find('abc', 'b'))\n",
             "write_text('out.txt', replace(read_text('in.txt'), 'a', 'b'))\n",
             "print(path_exists('x'), starts_with('abc', 'a'), ends_with('abc', 'c'))\n"})
        accepts(source);
    for (const auto& [source, code] : std::vector<std::pair<std::string, std::string>>{
             {"x = []\n", "E3014"},
             {"x = [1, true]\n", "E3002"},
             {"x: list[int] = [1.5]\n", "E3002"},
             {"x: list[void] = []\n", "E2003"},
             {"x = [print(1)]\n", "E3002"},
             {"print([1][true])\n", "E3002"},
             {"print(1[0])\n", "E3002"},
             {"print([1] < [2])\n", "E3002"},
             {"print([1] == ['1'])\n", "E3002"},
             {"x = [1]\nx[0] = 'a'\n", "E3002"},
             {"'abc'[0] = 'x'\n", "E3015"},
             {"[1][0] = 2\n", "E3015"},
             {"append([1], 2)\n", "E3015"},
             {"pop([1])\n", "E3015"},
             {"x = [1]\nappend(x, 'a')\n", "E3002"},
             {"append('x', 'y')\n", "E3002"},
             {"args(1)\n", "E3003"},
             {"split('a')\n", "E3003"},
             {"join([1], ',')\n", "E3002"},
             {"x = [1)\n", "E1006"},
             {"append = 1\n", "E3001"},
             {"list = 1\n", "E3001"},
             {"x = [1]\ny: list[float] = x\n", "E3002"},
             {"append()\n", "E3003"},
             {"print(pop())\n", "E3003"}})
        rejects(source, code);
    std::string nested_type = "x: ";
    for (int i = 0; i < 65; ++i)
        nested_type += "list[";
    nested_type += "int" + std::string(65, ']') + " = []\n";
    rejects(nested_type, "E2002");
    rejects("x = " + std::string(65, '[') + "1" + std::string(65, ']') + "\n", "E2002");
    for (const auto& source : std::vector<std::string>{
             "fn f() -> int:\n    raise 'no value'\n",
             "fn f(text: str) -> int:\n    try:\n        return int(text)\n    except err:\n       "
             " print(err.code, err.message, err.file, err.line, err.column)\n        return 0\n",
             "try:\n    raise 'bad'\nexcept err:\n    raise err\n", "fn f() -> int:\n    exit(2)\n",
             "eprint('message')\n"})
        accepts(source);
    for (const auto& [source, code] : std::vector<std::pair<std::string, std::string>>{
             {"raise 1\n", "E3017"},
             {"try:\n    pass\n", "E2001"},
             {"try:\n    pass\nexcept err:\n    print(err.missing)\n", "E3016"},
             {"x = 1\nprint(x.message)\n", "E3016"},
             {"raise 'bad'\nprint('unreachable')\n", "E3008"},
             {"if true:\n    import math\n", "E2011"},
             {"from math import gcd\n", "E3100"},
             {"exit(true)\n", "E3002"}})
        rejects(source, code);
    for (const auto& source : std::vector<std::string>{
             "x = json([])\ny = json({})\nprint(json_dump(x))\n",
             "record Data:\n    items: list[int]\nx = json(Data([1,2]))\n",
             "fn json_identity(n: int) -> int:\n    return n\nassert(json_identity(1) == 1)\n",
             "items: list[list[int]] = [[]]\nassert([] in items)\n"})
        accepts(source);
    for (const auto& source : std::vector<std::string>{
             "values = [1,2]\nkeys = 'text'\nget = 1\nslice = true\njson = 'existing name'\n",
             "fn get(value: int) -> int:\n    return value\nassert(get(4) == 4)\n",
             "fn json(value: str) -> str:\n    return value\nprint(json('unchanged'))\n",
             "fn get(a: int, b: int, c: int) -> int:\n    return a+b+c\nassert(get(1,2,3)==6)\n",
             "record = 1\nassert(record == 1)\n"})
        accepts(source);
    for (const auto& [source, code] : std::vector<std::pair<std::string, std::string>>{
             {"json_dump({1: 2})\n", "E3022"},
             {"record A:\n    data: dict[int,int]\njson(A({1:2}))\n", "E3022"},
             {"json_int(1)\n", "E3002"},
             {"json_parse(1)\n", "E3002"},
             {"json_null(1)\n", "E3003"},
             {"assert(json(1) < json(2))\n", "E3002"}})
        rejects(source, code);
    std::string deep = "print(" + std::string(257, '(') + "1" + std::string(257, ')') + ")\n";
    accepts("assert(byte_value(byte_string(255)) == 255)\n");
    accepts("x = ~1 & 7 | 2 ^ 3\nassert(1 + 2 << 3 == 24)\n");
    for (const auto& source : {"1.0 << 1\n", "1 >> true\n", "~1.5\n", "'a' & 'b'\n"})
        rejects(source, "E3002");
    rejects("byte_value(1)\n", "E3002");
    rejects("byte_string('a')\n", "E3002");
    for (const auto& source : std::vector<std::string>{
             "d: dict[str, int] = {}\nd['a'] = 1\nprint(keys(d), values(d), get(d, 'b', 2))\n",
             "d = {1: 2, 2: 3}\nfor key in d:\n    remove(d, key)\n",
             "assert('a' in {'a': 1})\nassert(1 not in [2, 3])\nassert('ab' in 'abc')\n",
             "record Point:\n    x: float\n    y: float\np = Point(1, 2)\np.x = 3\n",
             "fn f(v: Point) -> Point:\n    return v\nrecord Point:\n    x: int\n",
             "record Tree:\n    children: list[Tree]\nt = Tree([])\nappend(t.children, Tree([]))\n",
             "record Catalog:\n    values: dict[str, Catalog]\nc = Catalog({})\n",
             "record Empty:\n    pass\ne = Empty()\n",
             "record A:\n    b: B\nrecord B:\n    x: int\na = A(B(1))\na.b.x = 2\n"})
        accepts(source);
    for (const auto& [source, code] : std::vector<std::pair<std::string, std::string>>{
             {"d = {}\n", "E3014"},
             {"d: dict[float,int] = {}\n", "E3020"},
             {"d = {1.0: 1}\n", "E3020"},
             {"d = {'a': 1}\nprint(d[1])\n", "E3002"},
             {"d = {'a': 1}\nd['a'] = 'wrong'\n", "E3002"},
             {"print(get({'a': 1}, 'a', 'wrong'))\n", "E3002"},
             {"remove({'a': 1}, 'a')\n", "E3015"},
             {"assert({'a': 1} < {'b': 1})\n", "E3002"},
             {"assert(1 in 2)\n", "E3002"},
             {"record A:\n    x: int\n    x: str\n", "E3001"},
             {"record A:\n    x: A\n", "E3021"},
             {"record A:\n    x: B\nrecord B:\n    x: A\n", "E3021"},
             {"record A:\n    x: Missing\n", "E2003"},
             {"record A:\n    x: int\na = A()\n", "E3003"},
             {"record A:\n    x: int\na = A('wrong')\n", "E3002"},
             {"record A:\n    x: int\na = A(1)\nprint(a.missing)\n", "E3016"},
             {"record A:\n    x: int\na = A(1)\na.x = 'wrong'\n", "E3002"},
             {"record A:\n    x: int\nA(1).x = 2\n", "E3015"},
             {"record A:\n    x: int\nrecord B:\n    x: int\nassert(A(1) == B(1))\n", "E3002"},
             {"record A:\n    x: int\nassert(A(1) < A(2))\n", "E3002"},
             {"if true:\n    record A:\n        x: int\n", "E2012"}})
        rejects(source, code);
    rejects(deep, "E1006");
    deep = "x = ";
    for (int i = 0; i < 300; ++i)
        deep += "1 + ";
    deep += "1\n";
    rejects(deep, "E2002");
    // Limits describe the longest direct containment path, independent of DFS order.
    for (int edges : {255, 256, 257, 300}) {
        for (bool reversed : {false, true}) {
            std::string source;
            for (int step = 0; step <= edges; ++step) {
                const int i = reversed ? edges - step : step;
                source += "record R" + std::to_string(i) + ":\n    value: ";
                source += i == edges ? "int\n" : "R" + std::to_string(i + 1) + "\n";
            }
            if (edges <= 256)
                accepts(source);
            else
                rejects(source, "E2002");
        }
    }
    accepts("x = " + std::string(250, '(') + "1" + std::string(250, ')') + "\n");
    accepts("x = " + std::string(200, '-') + "1\n");
    accepts("x = " + std::string(64, '[') + "1" + std::string(64, ']') + "\n");
    std::string calls = "fn identity(value: int) -> int:\n    return value\nx = ";
    for (int i = 0; i < 200; ++i)
        calls += "identity(";
    accepts(calls + "1" + std::string(200, ')') + "\n");
    std::string constructors = "record Box:\n    children: list[Box]\nbox = ";
    for (int i = 0; i < 32; ++i)
        constructors += "Box([";
    constructors += "Box([])";
    for (int i = 0; i < 32; ++i)
        constructors += "])";
    accepts(constructors + "\n");
    deep = "if true:\n    pass\n";
    for (int i = 0; i < 260; ++i)
        deep += "elif false:\n    pass\n";
    rejects(deep, "E2002");
    // Deterministic malformed-input smoke fuzzing catches parser cursor errors.
    // Every input must either produce code or a normal source diagnostic.
    std::mt19937 random(0x574f4f44);
    const std::string alphabet = "abc0123_ :()[]=+*-/'\"\n#,.<>!";
    for (int sample = 0; sample < 2000; ++sample) {
        std::string source;
        const auto length = random() % 160;
        for (std::uint32_t i = 0; i < length; ++i)
            source += alphabet[random() % alphabet.size()];
        try {
            (void)wood::compile(source, "fuzz.wd");
            ++checks;
        } catch (const wood::Diagnostic&) {
            ++checks;
        } catch (const std::exception& error) {
            check(false, "malformed-input exception: " + std::string(error.what()));
        }
    }
    const auto tokens = wood::lex("# comment\r\nx = 2\r\n");
    accepts("fn inc(x: int) -> int:\n    return x + 1\nf: fn(int) -> int = inc\nprint(f(2))\n");
    accepts("extern fn native_add(x: int, y: float) -> float\nf = native_add\nprint(f(1,2))\n");
    accepts("extern = 1\nprint(extern)\n");
    accepts("time_sleep(0)\nprint(time_monotonic(), time_monotonic_ns())\n");
    accepts("value: str | None = env_get('WOOD_TEST')\n");
    accepts("fn env_get(value: int) -> int:\n    return value\nprint(env_get(1))\n");
    rejects("env_get(1)\n", "E3002");
    rejects("env_get()\n", "E3003");
    rejects("env_get('A', 'B')\n", "E3003");
    accepts("value: str = random_bytes(16)\ntask_checkpoint()\n");
    accepts("fn random_bytes(n: str) -> str:\n    return n\nprint(random_bytes('user'))\n");
    rejects("random_bytes('bad')\n", "E3002");
    rejects("random_bytes()\n", "E3003");
    rejects("task_checkpoint(1)\n", "E3003");
    accepts("time_sleep = 7\ntime_sleep(0)\n");
    accepts("fn time_sleep(value: str) -> str:\n    return value\nprint(time_sleep('user'))\n");
    rejects("time_sleep('bad')\n", "E3002");
    rejects("time_sleep(true)\n", "E3002");
    rejects("time_sleep()\n", "E3003");
    rejects("time_sleep(1, 2)\n", "E3003");
    rejects("time_monotonic(1)\n", "E3003");
    rejects("time_monotonic_ns(1)\n", "E3003");
    rejects("x = time_sleep(0)\n", "E3002");
    accepts("byte_value = 17\nprint(byte_value, byte_value('A'))\n");
    accepts("fn f(x: int) -> int:\n    return x\nbyte_value = f\nprint(byte_value(7))\n");
    accepts(
        "fn f() -> int:\n    return 1\nrecord R:\n    f: fn() -> int\nr = R(f)\nprint(r.f())\n");
    rejects("x: fn(void) -> int = 1\n", "E2003");
    rejects("x: fn(int) = 1\n", "E2001");
    rejects("fn f(x: int) -> int:\n    return x\nx: fn(float) -> int = f\n", "E3002");
    rejects("fn f() -> int:\n    return 1\nx = f\nx(1)\n", "E3003");
    rejects("fn f() -> int:\n    return 1\nx = f\njson_dump([x])\n", "E3022");
    rejects("fn f() -> int:\n    return 1\nprint(f < f)\n", "E3002");
    rejects("x = [1]\nx[0]()\n", "E3002");
    rejects("fn f() -> int:\n    return 1\nx: fn() -> float = f\n", "E3002");
    rejects("fn f():\n    pass\nx = f\ny = x()\n", "E3002");
    rejects("x = print\n", "E3007");
    accepts("extern fn f(x: str) -> int\n");
    rejects("extern fn f(x: bool)\n", "E3023");
    accepts("extern fn f(x: fn(int) -> int)\n");
    rejects("extern fn f(x: fn(str) -> int)\n", "E3023");
    rejects("extern fn f(x: fn(int) -> str)\n", "E3023");
    rejects("extern fn f() -> list[int]\n", "E3023");
    rejects("extern fn template()\n", "E3023");
    rejects("extern fn _reserved()\n", "E3023");
    rejects("extern fn f():\n    pass\n", "E2001");
    rejects("if true:\n    extern fn f()\n", "E2006");
    rejects("fs_list('x', 'wrong')\n", "E3002");
    rejects("fs_size()\n", "E3003");
    std::string nested_callable = "int";
    for (int i = 0; i < 65; ++i)
        nested_callable = "fn() -> " + nested_callable;
    rejects("fn f(x: " + nested_callable + "):\n    pass\n", "E2002");
    std::string long_target = "x";
    for (int i = 0; i < 300; ++i)
        long_target += ".f";
    rejects("x = 1\n" + long_target + "()\n", "E2002");
    check(tokens[0].location.line == 2 && tokens[0].location.column == 1 &&
              tokens[0].location.offset == 11,
          "CRLF locations");
    const auto code = wood::compile("x = 0009\n", "test.wd");
    check(code.find("wd::Int{9}") != std::string::npos, "canonical decimal literals");
    const wood::Diagnostic diagnostic("E3007", {6, 2, 3}, "undefined \"x\"");
    const auto json = wood::render_diagnostic(diagnostic, "a\\b.wd", "pass\n  x\n", true);
    check(wood::Json::parse(json).dump() ==
              wood::Json::parse(
                  "{\"severity\":\"error\",\"code\":\"E3007\",\"file\":\"a\\\\b.wd\",\"line\":2,"
                  "\"column\":3,\"message\":\"undefined \\\"x\\\"\"}\n")
                  .dump(),
          "JSON diagnostic escaping");
    check(wood::render_diagnostic(diagnostic, "test.wd", "pass\n  x\n").find("  ^") !=
              std::string::npos,
          "diagnostic caret");
    std::cout << checks << " compiler checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
