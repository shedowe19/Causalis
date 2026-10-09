#include "causalis/page.hpp"
#include "causalis/checkpoint.hpp"
#include "causalis/document.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string text(const causalis::Frame& f){std::string out;for(const auto& p:f.display_list)if(p.kind==causalis::PaintKind::Text)out+=p.text+" ";return out;}
}
int main(){
 try {
    const std::string html="<p id='result'>Original</p><script>document.querySelector('#result').textContent='Changed';console.log('done');</script>";
    auto disabled=causalis::render_page(html,640);
    expect(text(disabled.frame).find("Original")!=std::string::npos,"Scripts must be opt-in");
    causalis::PageOptions local;local.run_scripts=true;
    auto changed=causalis::render_page(html,640,local);
    expect(text(changed.frame).find("Changed")!=std::string::npos,"Local runtime must mutate the rendered document");
    expect(!changed.console.empty(),"Console must remain available outside page paint");
    local.source=causalis::DocumentSource::Remote;
    auto remote=causalis::render_page(html,640,local);
    expect(text(remote.frame).find("Original")!=std::string::npos,"Remote execution must be rejected");
    expect(!remote.script_diagnostics.empty(),"Remote execution rejection needs a diagnostic");
    local.source=causalis::DocumentSource::Local;
    auto bounded=causalis::render_page("<p id='result'>Original</p><script>for(let i=0;i<300;i++){document.getElementById('result').textContent='Changed';}</script>",640,local);
    expect(text(bounded.frame).find("Original")!=std::string::npos,"Mutation count exhaustion must discard all changes");
    expect(!bounded.script_diagnostics.empty(),"Mutation budget must be visible");
    std::string large="<p id='result'>Original</p>";
    large+=std::string(1024*1024,' ');
    large+="<script>for(let i=0;i<20;i++){document.getElementById('result').textContent='Changed';}</script>";
    auto work_bounded=causalis::render_page(large,640,local);
    expect(text(work_bounded.frame).find("Original")!=std::string::npos,"Reparse work exhaustion must discard all changes");
    causalis::Document doc("<title>Project</title><h1>Hello</h1><script>alert('secret')</script><form><input type=password value=secret></form>");
    auto checkpoint=causalis::make_checkpoint(doc,"https://example.invalid/project");
    expect(checkpoint.passive_html.find("<script")==std::string::npos,"Checkpoint must be passive");
    expect(checkpoint.passive_html.find("secret")==std::string::npos,"Checkpoint must omit executable and form content");
    auto encoded=causalis::encode_checkpoint(checkpoint);
    auto decoded=causalis::decode_checkpoint(encoded);
    expect(decoded.origin==checkpoint.origin,"Checkpoint origin must round-trip");
    auto newer=causalis::make_checkpoint(causalis::Document("<title>Project</title><h1>New text</h1>"),checkpoint.origin);
    expect(!causalis::compare_checkpoints(checkpoint,newer).empty(),"Changed checkpoint text must be detected");
    auto long_before=causalis::make_checkpoint(causalis::Document("<p>"+std::string(5000,'a')+"x</p>"),"local");
    auto long_after=causalis::make_checkpoint(causalis::Document("<p>"+std::string(5000,'a')+"y</p>"),"local");
    auto tail_change=causalis::compare_checkpoints(long_before,long_after);
    expect(!tail_change.empty() && tail_change.front().kind==causalis::CheckpointChange::Kind::Notice,"Bounded comparison must report differing omitted text");
    auto moved_before=causalis::make_checkpoint(causalis::Document("<p>One</p><p>Two</p>"),"local");
    auto moved_after=causalis::make_checkpoint(causalis::Document("<p>Two</p><p>One</p>"),"local");
    expect(!causalis::compare_checkpoints(moved_before,moved_after).empty(),"Reordered text must not be called unchanged");
    bool rejected=false;try{(void)causalis::decode_checkpoint(encoded+"junk");}catch(const std::exception&){rejected=true;}
    expect(rejected,"Checkpoint parser must reject trailing input");
    rejected=false;try{(void)causalis::decode_checkpoint(encoded.substr(0,encoded.size()-1));}catch(const std::exception&){rejected=true;}
    expect(rejected,"Checkpoint parser must reject truncation");
    auto rechecked=causalis::decode_checkpoint(causalis::encode_checkpoint({"local","bad","<p>Keep</p><script>secret()</script>"}));
    expect(rechecked.passive_html.find("<script")==std::string::npos,"Imported checkpoint must be sanitized again");
    std::cout<<"Page execution policy, mutation integration, reading snapshots, comparison and corrupt checkpoint checks passed.\n";
    return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
