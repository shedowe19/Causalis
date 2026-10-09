#include "causalis/script.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view explanation) {
    if (!condition) throw std::runtime_error(std::string(explanation));
}

causalis::script::Result run(std::string_view source, std::size_t budget = 100'000) {
    auto result = causalis::script::execute(source, budget);
    if (!result.success) {
        const auto reason = result.diagnostics.empty() ? "unknown failure" : result.diagnostics.front();
        throw std::runtime_error("Script unexpectedly failed: " + reason);
    }
    require(result.diagnostics.empty(), "successful script must have no error diagnostics");
    return result;
}

void expect_console(std::string_view source, std::vector<std::string> expected) {
    require(run(source).console == expected, "unexpected console values");
}

void expect_failure(std::string_view source, std::string_view diagnostic = {}, std::size_t budget = 100'000) {
    const auto result = causalis::script::execute(source, budget);
    require(!result.success, "script must report failure");
    require(!result.diagnostics.empty(), "failure must explain why execution stopped");
    require(result.console.empty() && result.mutations.empty(), "failure must roll back all outputs");
    if (!diagnostic.empty()) require(result.diagnostics.front().find(diagnostic) != std::string::npos,
                                     "failure diagnostic must identify the expected cause");
}

struct Test { const char* name; std::function<void()> run; };

} // namespace

int main() {
    const std::vector<Test> tests = {
        {"empty script", [] { require(run("").mutations.empty(), "empty source must not invent mutations"); }},
        {"comments and arithmetic precedence", [] {
            expect_console("// comment\n/* another */ console.log(2 + 3 * 4, (2 + 3) * 4, .5 + 1e2);", {"14 20 100.5"});
        }},
        {"primitive values and numeric conversion", [] {
            expect_console("let unset; console.log(unset, null, true, false, String(3), Number(' +12.5 '), Boolean(''), Boolean('x'));", {"undefined null true false 3 12.5 false true"});
        }},
        {"empty console arguments retain separators", [] { expect_console("console.log('', 'x', '', '');", {" x  "}); }},
        {"string length counts UTF-16 code units", [] { expect_console("console.log('A€🚀'.length);", {"4"}); }},
        {"string concatenation and escapes", [] {
            expect_console(R"(console.log('A\nB\t' + 3, '\x41\u20ac\ud83d\ude80', "\"'\\" );)", {"A\nB\t3 A€🚀 \"'\\"});
        }},
        {"quoted keywords and delimiters are data", [] {
            expect_console(R"({ '}'; console.log("true", 'if', 'let', ';'); } function f() { return '}'; } console.log(f());)", {"true if let ;", "}"});
        }},
        {"assignment and update evaluation order", [] {
            expect_console("let a = 1, b = 2; a += (a = 7); b = a = 3; console.log(a, b, a++, ++a, a--, --a);", {"3 3 3 5 5 3"});
        }},
        {"assignment is right associative", [] {
            expect_console("let a=0,b=0,c=0; a=b=c=7; console.log(a,b,c);", {"7 7 7"});
        }},
        {"short circuit returns values and prevents errors", [] {
            expect_console("console.log(false && unknown, 'a' || unknown, 0 || 5, true && 'yes');", {"false a 5 yes"});
        }},
        {"ternary and conditional branches", [] {
            expect_console("let x=4; if(x>2) { console.log('high'); } else { unknown(); } console.log(x===4 ? 'yes' : unknown);", {"high", "yes"});
        }},
        {"equality and lexical comparison", [] {
            expect_console("console.log(2 == '2', 2 === '2', null == undefined, null === undefined, '10' < '2', 3 >= 3, false != 0, 'x' == 5);", {"true false true false true true false false"});
        }},
        {"block scope and var function scope", [] {
            expect_console("let x=1; {let x=2; var y=3; console.log(x);} console.log(x,y); var y; console.log(y);", {"2", "1 3", "3"});
        }},
        {"for loop with continue and break", [] {
            expect_console("let total=0; for(let i=0;i<10;i++){ if(i===2)continue; if(i===6)break; total+=i; } console.log(total);", {"13"});
        }},
        {"while loop and nested loop control", [] {
            expect_console("let i=0, n=0; while(i<3){ i++; for(let j=0;j<3;j++){ if(j===1)break; n++; }} console.log(i,n);", {"3 3"});
        }},
        {"for without initializer condition or increment", [] {
            expect_console("let i=0; for(;;){ if(++i===3)break; } console.log(i);", {"3"});
        }},
        {"named functions parameters return and recursion", [] {
            expect_console("function fact(n){if(n<=1)return 1; return n*fact(n-1);} console.log(fact(6));", {"720"});
        }},
        {"lexical closure and function alias", [] {
            expect_console("function make(x){function f(y){return x+y;}return f;} const add=make(4); console.log(add(3));", {"7"});
        }},
        {"for let closures retain per iteration values", [] {
            expect_console("let first,second;for(let i=0;i<2;i++){function f(){return i;}if(i===0)first=f;else second=f;}console.log(first(),second());", {"0 1"});
        }},
        {"function return exits loop", [] {
            expect_console("function find(){for(let i=0;i<10;i++){if(i===4)return i;}return 99;} console.log(find());", {"4"});
        }},
        {"missing arguments and undefined return", [] {
            expect_console("function f(x){console.log(x);}console.log(f());", {"undefined", "undefined"});
        }},
        {"newline return and automatic separators", [] {
            expect_console("function f(){ return\n3; }\nlet x=2\nconsole.log(x)\nconsole.log(f())", {"2", "undefined"});
        }},
        {"math builtins", [] {
            expect_console("console.log(Math.floor(3.9),Math.ceil(3.1),Math.round(-1.5),Math.abs(-2),Math.min(4,2,7),Math.max(1,8),Math.sqrt(9),Math.pow(2,5));", {"3 4 -1 2 2 8 3 32"});
        }},
        {"DOM text styles and safe attributes", [] {
            const auto result = run("const el=document.querySelector('#status'); el.textContent='Ready '+3; el.style.fontSize='24px'; el.style['color']='red'; el.setAttribute('title','Done'); el.className='active'; el.setAttribute('aria-label',5);");
            require(result.mutations.size() == 6, "must emit six mutations");
            require(result.mutations[0].selector == "#status" && result.mutations[0].property == "textContent" && result.mutations[0].value == "Ready 3", "text mutation must preserve selector and value");
            require(result.mutations[1].property == "style.font-size", "camelCase style names must normalize");
            require(result.mutations[2].property == "style.color", "computed style property must work");
            require(result.mutations[3].property == "attribute.title", "attribute must emit a typed mutation");
            require(result.mutations[4].property == "attribute.class", "className maps to class attribute");
            require(result.mutations[5].value == "5", "DOM value conversion must stringify");
        }},
        {"DOM write read and compound text assignment", [] {
            const auto result = run("const el=document.getElementById('x'); el.textContent='A'; el.textContent+='B'; console.log(el.textContent); el.style.setProperty('background-color','blue');console.log(el.style.backgroundColor);");
            require(result.console == std::vector<std::string>{"AB", "blue"}, "DOM reads must see this transaction's writes");
            require(result.mutations.back().property == "style.background-color", "setProperty must emit CSS property");
        }},
        {"only id selectors are accepted", [] {
            expect_failure("document.querySelector('.message').textContent='x';", "#id selector");
            expect_failure("document.querySelector('h1').textContent='x';", "#id selector");
        }},
        {"DOM initial read explicitly unsupported", [] { expect_failure("console.log(document.getElementById('x').textContent);", "Initial DOM reads"); }},
        {"missing variable is an error", [] { expect_failure("console.log(unknown);", "Undefined variable"); }},
        {"assignment cannot create implicit globals", [] { expect_failure("unknown=4;", "Undefined variable"); }},
        {"const cannot be reassigned", [] { expect_failure("const x=1; x=2;", "const binding"); }},
        {"duplicate lexical binding is an error", [] { expect_failure("let x=1; let x=2;", "Duplicate binding"); }},
        {"lexical initializers cannot read their own shadow", [] { expect_failure("let x=1;{let x=x;}", "before initialization"); }},
        {"let does not escape a block", [] { expect_failure("{let secret=1;}console.log(secret);", "Undefined variable"); }},
        {"for let does not escape loop scope", [] { expect_failure("for(let i=0;i<1;i++){} console.log(i);", "Undefined variable"); }},
        {"function declarations are not hoisted", [] { expect_failure("f();function f(){}", "Undefined variable"); }},
        {"parse failure rolls back earlier statements", [] {
            expect_failure("document.querySelector('#x').textContent='pending'; console.log('pending'); let = ;");
        }},
        {"runtime failure rolls back earlier mutations", [] {
            expect_failure("document.querySelector('#x').textContent='pending'; console.log('pending'); missing();", "Undefined variable");
        }},
        {"loop instruction budget rolls back mutations", [] {
            expect_failure("document.querySelector('#x').textContent='pending'; console.log('pending');while(true){}", "instruction budget", 250);
        }},
        {"empty for loop has an instruction cost", [] { expect_failure("for(;;);", "instruction budget", 100); }},
        {"zero budget rejects execution", [] { expect_failure(";", "instruction budget", 0); }},
        {"recursive function depth is bounded", [] { expect_failure("function f(){return f();}f();", "depth limit"); }},
        {"deep syntax nesting is bounded", [] {
            expect_failure(std::string(500, '(') + "1" + std::string(500, ')') + ";", "nesting limit");
        }},
        {"long left associative tree is bounded before destruction", [] {
            std::string source="1"; for(int i=0;i<5'000;i++)source+="+1";source+=";";
            expect_failure(source, "expression-tree depth limit");
        }},
        {"long member chain is bounded", [] {
            std::string source="document"; for(int i=0;i<500;i++)source+=".x"; source+=";";
            expect_failure(source, "limit");
        }},
        {"source size is bounded", [] { expect_failure(std::string(1'048'577,' '), "source-size limit"); }},
        {"token and syntax node counts are bounded", [] {
            expect_failure(std::string(100'001,';'), "token limit");
            expect_failure(std::string(40'001,';'), "syntax-node limit");
        }},
        {"literal string size is bounded", [] { expect_failure("'"+std::string(65'537,'a')+"';", "string limit"); }},
        {"concatenated string size is bounded", [] {
            expect_failure("let x='a';for(let i=0;i<20;i++)x=x+x;", "string limit");
        }},
        {"output volume is bounded", [] { expect_failure("for(let i=0;i<3000;i++)console.log(i);", "console-entry limit"); }},
        {"output bytes are bounded", [] {
            expect_failure("const x='"+std::string(60'000,'a')+"';for(let i=0;i<30;i++)console.log(x);", "output-size limit");
        }},
        {"function registry is bounded", [] { expect_failure("for(let i=0;i<300;i++){function f(){return i;}}", "function-definition limit"); }},
        {"division by zero and overflow fail explicitly", [] {
            expect_failure("console.log(1/0);", "Division by zero");
            expect_failure("console.log(1e308*1e308);", "not finite");
            expect_failure("console.log(Math.sqrt(-1));", "not finite");
        }},
        {"malformed strings comments and escapes", [] {
            expect_failure("'unterminated", "Unterminated");
            expect_failure("/*unterminated", "Unterminated");
            expect_failure(R"('\ud800';)", "surrogate");
            expect_failure(R"('\u12xx';)", "hexadecimal");
            expect_failure(R"('\07';)", "Octal");
            expect_failure("'line\nbreak';", "Invalid character");
        }},
        {"unsafe DOM features are rejected atomically", [] {
            expect_failure("document.querySelector('#x').innerHTML='<script>x</script>';", "unsafe element assignment");
            expect_failure("document.querySelector('#x').setAttribute('onclick','run()');", "unsafe attribute");
            expect_failure("document.querySelector('div > a').textContent='x';", "selector is supported");
            expect_failure("document.getElementById('x').textContent='pending';document.getElementById('x').id='y';", "unsafe element assignment");
            expect_failure("const el=document.getElementById('x');el.setAttribute('id','y');el.textContent='z';", "unsafe attribute");
            for(const auto& attribute: {"href","src","value","alt","data-id","aria-live"})expect_failure("document.getElementById('x').setAttribute('"+std::string(attribute)+"','x');","unsafe attribute");
            expect_failure("document.getElementById('x').value=3;", "unsafe element assignment");
        }},
        {"adapter safe attribute contract", [] {
            const auto result=run("const e=document.getElementById('x');e.setAttribute('class','x');e.setAttribute('title','x');e.setAttribute('lang','de');e.setAttribute('dir','ltr');e.setAttribute('role','status');e.setAttribute('hidden','');e.setAttribute('aria-label','x');e.setAttribute('aria-hidden','true');");
            require(result.mutations.size()==8,"all eight adapter attributes must be accepted");
        }},
        {"DOM mutation count matches adapter limit", [] { expect_failure("const e=document.getElementById('x');for(let i=0;i<257;i++)e.textContent=i;","DOM-mutation limit"); }},
        {"adapter CSS property and value contract", [] {
            expect_failure("document.getElementById('x').style.backgroundImage='url(http://example.com)';","Unsupported style property");
            expect_failure("document.getElementById('x').style.color='red;display:none';","Unsafe style value");
            expect_failure("document.getElementById('x').style.background='url(test)';","unsafe CSS function");
            expect_failure("document.getElementById('x').style.display='flex';","Unsupported display value");
            expect_failure("document.getElementById('x').setAttribute('title','"+std::string(4'097,'a')+"');","attribute-value limit");
            expect_failure("document.getElementById('x').style.color='"+std::string(4'097,'a')+"';","oversized style value");
            const auto result=run("const e=document.getElementById('x');e.style.color=' rgb(20, 40, 60) ';e.style.maxWidth='80%';");
            require(result.mutations[0].value=="rgb(20, 40, 60)"&&result.mutations[1].property=="style.max-width","allowed CSS values must normalize and preserve supported properties");
        }},
        {"no operating system networking or credential builtins", [] {
            expect_failure("fetch('https://example.com');", "Undefined variable");
            expect_failure("require('fs');", "Undefined variable");
            expect_failure("console.log(document.cookie);", "Unsupported host property");
            expect_failure("console.log(document.constructor);", "Unsupported host property");
            expect_failure("console.log(window);", "Undefined variable");
            expect_failure("eval('1');", "Undefined variable");
        }},
        {"unsupported language syntax fails clearly", [] {
            expect_failure("const x=[];", "Unsupported");
            expect_failure("const x={};", "Unsupported");
            expect_failure("const x=()=>1;", "Unsupported");
            expect_failure("console.log(`x`);", "Template literals");
            expect_failure("new Thing();", "Unsupported");
            expect_failure("class X{}", "Unsupported");
        }},
        {"illegal control flow is rejected at parse time", [] {
            expect_failure("return 1;", "outside a function");
            expect_failure("break;", "outside a loop");
            expect_failure("while(true){function f(){break;}}", "outside a loop");
        }},
        {"function declaration requires a real block token", [] { expect_failure("function f() '{';", "block body"); }},
        {"malformed statements do not silently join", [] { expect_failure("console.log(1) console.log(2)", "separator"); }},
        {"invalid lvalues and calls fail", [] {
            expect_failure("(1+2)=3;", "assignment target");
            expect_failure("let x=1;x();", "not callable");
            expect_failure("console.log(1).x=3;", "assignable");
        }},
        {"lexical strings consume execution budget", [] {
            expect_failure("let x='"+std::string(60'000,'a')+"';while(true){let y=x;}", "instruction budget", 3'000);
        }},
        {"host argument validation", [] {
            expect_failure("document.querySelector();", "requires 1 argument");
            expect_failure("document.querySelector('#x').setAttribute('title');", "requires 2 argument");
            expect_failure("Math.pow(2);", "requires 2 argument");
        }},
        {"deterministic malformed source stress", [] {
            const std::string alphabet="abcdefghijklmnopqrstuvwxyz0123456789{}[]().;,+-*/!<>=&|?:\\\"' \n\r\t";
            unsigned state=0xcau;
            for(unsigned sample=0;sample<3'000;++sample){
                std::string source;
                state=state*1664525u+1013904223u;
                const unsigned length=state%256;
                for(unsigned i=0;i<length;++i){state=state*1664525u+1013904223u;source.push_back(alphabet[state%alphabet.size()]);}
                const auto result=causalis::script::execute(source,1'000);
                if(!result.success)require(result.console.empty()&&result.mutations.empty()&&!result.diagnostics.empty(),"malformed inputs must fail transactionally");
            }
        }},
    };
    std::size_t passed = 0;
    for (const auto& test : tests) {
        try { test.run(); ++passed; }
        catch (const std::exception& error) {
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " original scripting-runtime tests passed\n";
    return 0;
}
