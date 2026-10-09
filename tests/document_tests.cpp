#include "causalis/document.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
struct Test { const char* name; std::function<void()> run; };
}

int main() {
    const std::vector<Test> tests{
        {"source preservation and title entities", [] {
            const std::string source = "<!doctype html><head><TITLE>A &amp; B &#x1F680;</TITLE></head><p>Hi</p>";
            const causalis::Document doc(source);
            require(doc.html() == source, "inspection must preserve exact source");
            require(doc.title() == "A & B \xF0\x9F\x9A\x80", "title must decode numeric and named entities");
            require(doc.plain_text() == "Hi", "head metadata must be omitted from plain text");
        }},
        {"exact quoted id and safe text mutation", [] {
            causalis::Document doc("<p data-id='goal' title=\"id='goal'\">Keep</p><p ID = 'goal'>Old<b>child</b></p>");
            require(doc.set_text("#goal", "<img onerror='x'> & \"quote\""), "actual id must be found");
            require(doc.html().find(">Keep</p>") != std::string::npos, "attribute substrings must not match ids");
            require(doc.html().find("&lt;img onerror=&#39;x&#39;&gt; &amp; &quot;quote&quot;") != std::string::npos,
                    "textContent must be markup escaped");
            require(doc.html().find("<b>child</b>") == std::string::npos, "textContent replaces the whole child range");
        }},
        {"quoted greater-than signs", [] {
            causalis::Document doc("<p title='> fake id=wrong' id=right>old</p>");
            require(!doc.set_text("#wrong", "bad"), "id text inside a quote must never match");
            require(doc.set_text("#right", "correct"), "quotes must not prematurely terminate an opening tag");
            require(doc.plain_text() == "correct", "new text must be readable");
        }},
        {"entity and UTF-8 ids", [] {
            causalis::Document doc("<p id='caf&#233;'>First</p><p id='Grüße'>Second</p>");
            require(doc.set_text("#café", "é"), "numeric entities in IDs must be decoded");
            require(doc.set_text("#Grüße", "Grüße 世界"), "UTF-8 selector/value must remain intact");
            require(doc.plain_text() == "é\nGrüße 世界", "UTF-8 text must survive round trip");
        }},
        {"duplicate ids and duplicate attributes", [] {
            causalis::Document doc("<p id='first' id='ignored'>one</p><p id='first'>two</p>");
            require(!doc.set_text("#ignored", "wrong"), "the first duplicate attribute wins");
            require(doc.set_text("#first", "changed"), "first matching element must be mutable");
            require(doc.plain_text() == "changed\ntwo", "duplicate ID mutation must change only the first element");
            require(doc.set_attribute("#first", "id", "new"), "id attributes must be safely replaceable");
            require(doc.html().find("id='ignored'") == std::string::npos, "duplicate mutated attributes must be removed");
            require(doc.set_text("#new", "again"), "changed id must take effect");
        }},
        {"comments and raw source are not document ids", [] {
            causalis::Document doc("<!--<p id=trap>x</p>--><script>var s = '<p id=trap>';</script><p id=real>y</p>");
            require(!doc.set_text("#trap", "z"), "comments/script strings must not invent elements");
            require(doc.set_text("#real", "z"), "actual body element must remain available");
            require(doc.scripts().size() == 1, "real inline script must be inspectable");
        }},
        {"raw-text close tags require a name boundary", [] {
            const causalis::Document doc("<script>one </scriptx> two</ScRiPt ><p>Visible</p>");
            require(doc.scripts() == std::vector<std::string>{"one </scriptx> two"}, "partial closing names must not end raw text");
            require(doc.plain_text() == "Visible", "raw source must stay invisible");
        }},
        {"inert and unsupported script categories", [] {
            const causalis::Document doc("<template><script>inert()</script></template>"
                "<script type='application/json'>{}</script><script type=module>module()</script>"
                "<script src='https://example.invalid/x'>ignored()</script><noscript><script>fallback()</script></noscript>"
                "<svg><script>foreign()</script></svg><script type=' TEXT/JAVASCRIPT '>classic()</script>"
                "<script type='application/javascript'>classic2()</script>");
            require(doc.scripts() == std::vector<std::string>{"classic()", "classic2()"}, "only supported real classic inline scripts are returned");
        }},
        {"inert ids and raw/void mutation restrictions", [] {
            causalis::Document doc("<template><p id=x>inert</p></template><p id=x>real</p><script id=script>old</script><br id=br>");
            require(doc.set_text("#x", "changed"), "template contents must not shadow live ids");
            require(!doc.set_text("#script", "run()"), "raw executable source cannot be changed with text mutation");
            require(!doc.set_text("#br", "invalid"), "void tags do not have text content");
            require(!doc.set_text("p", "invalid"), "unsupported selectors must be rejected");
        }},
        {"reading view excludes executable and form subtrees", [] {
            const causalis::Document doc("<head><title>Safe</title><script>secret()</script></head>"
                "<p>Article</p><style>.secret{}</style><template>template secret</template>"
                "<form><label>Password</label><input value=secret><p>form secret</p></form>"
                "<div hidden>hidden secret</div><button>control secret</button><p>End</p>");
            const auto reading = doc.reading_html();
            require(reading.find("secret") == std::string::npos, "original executable/form/hidden content must not leak into reading view");
            require(doc.plain_text() == "Article\nEnd", "plain text must retain block boundaries");
            require(reading.find("<title>Safe</title>") != std::string::npos, "safe title must be retained");
        }},
        {"reading title excludes hidden and form ancestors", [] {
            const causalis::Document form("<form><title>FORM_SECRET</title></form><p>Public</p>");
            const causalis::Document hidden("<div hidden><title>HIDDEN_SECRET</title></div><p>Public</p>");
            require(form.title().empty() && hidden.title().empty(), "excluded ancestor titles must not become document metadata");
            require(form.reading_html().find("FORM_SECRET") == std::string::npos &&
                    hidden.reading_html().find("HIDDEN_SECRET") == std::string::npos,
                    "excluded titles must not leak through generated head metadata");
            const causalis::Document normal("<form><title>SECRET</title></form><head><title>Normal</title></head><p>Public</p>");
            require(normal.title() == "Normal", "a normal head title must remain available after excluded titles");
        }},
        {"reading view sanitizes link schemes and event attributes", [] {
            const causalis::Document doc("<a href='javascript:alert(1)' onclick='x'>one</a>"
                "<a href='&#106;avascript:alert(2)'>two</a><a href='data:text/html,x'>three</a>"
                "<a href='file:///secret'>four</a><a href='https://example.test/?a=1&amp;b=2' onclick=x>good</a>"
                "<a href='../relative#part'>relative</a><a href='#part'>fragment</a>");
            const auto reading = doc.reading_html();
            require(reading.find("javascript:") == std::string::npos && reading.find("data:") == std::string::npos &&
                    reading.find("file:") == std::string::npos && reading.find("onclick") == std::string::npos,
                    "unsafe navigation/event attributes must be removed");
            require(reading.find("href=\"https://example.test/?a=1&amp;b=2\"") != std::string::npos,
                    "safe href must be attribute escaped");
            require(reading.find("href=\"../relative#part\"") != std::string::npos, "relative links are retained");
        }},
        {"generated text and title cannot break out of markup", [] {
            const causalis::Document doc("<title>&lt;/title&gt;&lt;script&gt;x&lt;/script&gt;</title>"
                "<p>&lt;img src=x onerror=x&gt; &amp; &#0; &#xD800;</p>");
            const auto reading = doc.reading_html();
            require(reading.find("<script>") == std::string::npos && reading.find("<img") == std::string::npos,
                    "decoded textual markup must always be re-escaped");
            require(reading.find("\xEF\xBF\xBD") != std::string::npos, "invalid numeric code points must become replacement characters");
        }},
        {"safe global attribute insertion", [] {
            causalis::Document doc("<p id=x>one</p>");
            require(doc.set_attribute("#x", "title", "\" ><script>x</script>"), "safe global attribute mutation must succeed");
            require(doc.html().find("title=\"&quot; &gt;&lt;script&gt;x&lt;/script&gt;\"") != std::string::npos,
                    "attribute content must be escaped");
            require(!doc.set_attribute("#x", "onclick", "x"), "event attributes are forbidden");
            require(!doc.set_attribute("#x", "href", "https://example.test"), "navigation attributes are forbidden");
            require(!doc.set_attribute("#x", "src", "https://example.test"), "resource attributes are forbidden");
            require(!doc.set_attribute("#x", "style", "color:red"), "style must use the dedicated typed method");
            require(!doc.set_attribute("#x", "title", std::string(4097, 'a')), "attribute values must be bounded");
        }},
        {"safe visual style update preserves validated declarations", [] {
            causalis::Document doc("<p id=x style='color:red;padding:8px;background:url(https://bad);position:fixed'>one</p>");
            require(doc.set_style("#x", "color", "rgb(1, 2, 3)"), "supported color must be settable");
            require(doc.html().find("padding:8px;color:rgb(1, 2, 3);") != std::string::npos, "safe visual declarations must be retained");
            require(doc.html().find("url(") == std::string::npos && doc.html().find("position") == std::string::npos,
                    "unsupported/resource declarations are dropped during typed style mutation");
            require(doc.set_style("#x", "font-size", "18px"), "font size must be settable");
            require(!doc.set_style("#x", "color", "red;display:none"), "declaration injection must be rejected");
            require(!doc.set_style("#x", "background", "url(https://bad)"), "resource functions must be rejected");
            require(!doc.set_style("#x", "background", "rgb(1,2,3) url(foo)"), "a resource suffix after rgb must be rejected");
            require(!doc.set_style("#x", "color", "rgb(rgb(1,2,3),2,3)"), "nested functions must be rejected");
            require(!doc.set_style("#x", "color", "rgb(1,,3)"), "malformed color channel lists must be rejected");
            require(doc.set_style("#x", "color", "rgba(1,2,3,0.5)"), "one complete safe rgba function is allowed");
            require(!doc.set_style("#x", "display", "grid"), "unsupported display mode must be rejected");
            require(!doc.set_style("#x", "position", "fixed"), "unsupported property must be rejected");
            const std::string before = doc.html();
            require(!doc.set_style("#x", "font-size", std::string(4096, 'a')), "combined style attribute must respect the decoded-value budget");
            require(doc.html() == before, "over-budget style mutation must preserve source");
        }},
        {"malformed tags and optional closing boundaries", [] {
            causalis::Document doc("<p id=x>one<p>two<li>three<li>four<broken id='unclosed");
            require(doc.set_text("#x", "replaced"), "paragraph implicit close must delimit text mutation");
            require(doc.plain_text() == "replaced\ntwo\nthree\nfour", "optional ends must keep readable block boundaries");
            require(!doc.set_text("#unclosed", "no"), "unterminated opening tag must not be mutable");
        }},
        {"unclosed content and unmatched end tags remain bounded", [] {
            causalis::Document doc("</missing><div id=x><b>one</div>two");
            require(doc.set_text("#x", "replacement"), "ancestor close must close an unterminated descendant");
            require(doc.html() == "</missing><div id=x>replacement</div>two", "mutation range must stop at matching parent close");
            causalis::Document unclosed("<p id=x>tail");
            require(unclosed.set_text("#x", "new"), "unclosed element content can end at EOF");
            require(unclosed.html() == "<p id=x>new", "EOF text mutation must preserve source form");
        }},
        {"greater-than inside unterminated attribute cannot complete a tag", [] {
            const std::string source = "<p id=x title='unclosed >";
            causalis::Document doc(source);
            require(!doc.set_text("#x", "new"), "quoted greater-than cannot delimit mutable text content");
            require(!doc.set_attribute("#x", "title", "new"), "unterminated opening tags cannot be attribute mutated");
            require(!doc.set_style("#x", "color", "red"), "unterminated opening tags cannot be style mutated");
            require(doc.html() == source, "failed malformed mutations must preserve exact source");
        }},
        {"raw title and textarea text do not create script elements", [] {
            const causalis::Document doc("<title>Title <script>inert</script></title>"
                "<textarea><script>inert2</script></textarea><xmp><script>inert3</script></xmp>");
            require(doc.scripts().empty(), "RCDATA/raw-text elements do not parse nested scripts");
            require(doc.title() == "Title <script>inert</script>", "title raw text must be retained as text");
        }},
        {"byte checkpoint difference", [] {
            const causalis::Document doc("abcNEWxyz");
            const auto difference = doc.difference("abcOLDxyz");
            require(difference.changed && difference.first_changed_byte == 3 &&
                    difference.removed_bytes == 3 && difference.inserted_bytes == 3,
                    "diff must identify the changed range without duplicating shared prefix/suffix");
            require(!doc.difference(doc.html()).changed, "identical snapshots must have no difference");
            const causalis::Document empty("");
            require(empty.difference("x").removed_bytes == 1, "complete removals must be supported");
            require(doc.difference("").inserted_bytes == doc.html().size(), "complete insertions must be supported");
        }},
        {"document and nesting budgets", [] {
            bool rejected = false;
            try { const causalis::Document oversized(std::string(16U * 1024U * 1024U + 1U, 'a')); }
            catch (const std::length_error&) { rejected = true; }
            require(rejected, "oversized document must be rejected explicitly");
            std::string nested;
            for (int i = 0; i < 10000; ++i) nested += "<div>";
            nested += "depth secret";
            const causalis::Document deep(nested);
            require(deep.plain_text().empty(), "over-budget nesting tail must be omitted rather than partially interpreted");
            std::string many;
            for (int i = 0; i < 70000; ++i) many += "<br>";
            many += "node limit secret";
            const causalis::Document crowded(many);
            require(crowded.plain_text().empty(), "over-budget element tail must be omitted");
        }},
        {"attribute counts names and decoded values are bounded", [] {
            std::string many = "<p id=x ";
            for (int i = 0; i < 257; ++i) many += "a ";
            many += ">secret</p>";
            causalis::Document crowded(many);
            require(crowded.plain_text().empty() && crowded.scripts().empty(), "per-element attribute overflow must fail closed");
            require(!crowded.set_text("#x", "new"), "over-budget source must not expose mutable partial elements");
            require(crowded.html() == many, "attribute budgets must preserve original source");
            causalis::Document long_name("<p id=x " + std::string(129, 'a') + ">secret</p>");
            causalis::Document long_value("<p id=x title='" + std::string(4097, 'a') + "'>secret</p>");
            require(long_name.plain_text().empty() && long_value.plain_text().empty(), "oversized names/decoded values must fail closed");
            const causalis::Document entity_value("<p title='" + std::string(4000, 'a') + "&#x1F680;'>visible</p>");
            require(entity_value.plain_text() == "visible", "valid UTF-8 entity expansion within the byte budget is allowed");
            std::string total;
            for (int i = 0; i < 300; ++i) {
                total += "<p ";
                for (int j = 0; j < 250; ++j) total += "a ";
                total += ">secret</p>";
            }
            const causalis::Document global(total);
            require(global.plain_text().empty(), "global attribute cap must fail closed rather than accumulating vectors");
        }},
        {"malformed source smoke corpus", [] {
            std::uint32_t random = 0xCA05A11U;
            constexpr std::string_view alphabet = "<>/='\" abcdef0123&;\n\t";
            for (int case_index = 0; case_index < 1500; ++case_index) {
                std::string source;
                const std::size_t count = static_cast<std::size_t>(case_index % 300);
                for (std::size_t i = 0; i < count; ++i) {
                    random = random * 1664525U + 1013904223U;
                    source.push_back(alphabet[random % alphabet.size()]);
                }
                causalis::Document doc(source);
                const auto title = doc.title();
                const auto scripts = doc.scripts();
                const auto plain = doc.plain_text();
                const auto reading = doc.reading_html();
                require(reading.starts_with("<!doctype html>"), "reading must always be a complete generated document");
                require(!doc.difference(source).changed, "read-only operations must never change malformed source");
                (void)title; (void)scripts; (void)plain;
                (void)doc.set_text("#target", "<safe>");
                (void)doc.set_attribute("#target", "title", "safe");
                (void)doc.set_style("#target", "color", "#fff");
            }
        }},
    };
    std::size_t failures = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& exception) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << exception.what() << '\n';
        }
    }
    std::cout << (tests.size() - failures) << '/' << tests.size() << " document tests passed\n";
    return failures == 0 ? 0 : 1;
}
