#include "../plate_persistent/plate_quality.h"
#include "../plate_persistent/plate_consensus.h"
#include "../plate_persistent/stream_protocol.h"
#include <iostream>
#include <sstream>
#include <functional>

int tests = 0;
void require(bool condition, const char* message) {
    ++tests;
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
bool throws(const std::function<void()>& f) { try { f(); return false; } catch (...) { return true; } }
plate::Detection box(float x = 10) {
    plate::Detection d;
    d.box = {x,10,x+100,40}; d.confidence = .8f;
    d.corners = {{{x,10},{x+100,10},{x+100,40},{x,40}}};
    return d;
}
int main() {
    try {
        plate::QualityConfig q;
        require(plate::quality_reason(box(),640,480,q).empty(), "normal quadrilateral accepted");
        auto d=box();d.confidence=.1f;
        require(plate::quality_reason(d,640,480,q)=="score_below_threshold", "low final score rejected");
        q.enabled=false;
        require(plate::quality_reason(d,640,480,q).empty(), "baseline filter switch preserves candidates");
        q.enabled=true;d=box();d.corners[0].x=-1;
        require(plate::quality_reason(d,640,480,q)=="corner_outside_image", "out of bounds rejected");
        d=box();d.corners[1]=d.corners[0];
        require(!plate::quality_reason(d,640,480,q).empty(), "repeated corner rejected");
        d=box();std::swap(d.corners[1],d.corners[2]);
        require(!plate::quality_reason(d,640,480,q).empty(), "self intersecting corners rejected");
        d=box();d.box[2]=d.box[0]+5;
        require(plate::quality_reason(d,640,480,q)=="crop_below_minimum_size", "tiny crop rejected");
        d=box();d.corners[2].x=std::numeric_limits<float>::quiet_NaN();
        require(!plate::quality_reason(d,640,480,q).empty(), "NaN corner rejected");
        plate::Consensus c;
        require(c.observe(box(),"A",true,0).confirmed.empty(), "one vote remains pending");
        c.observe(box(),"B",true,1000);
        require(c.observe(box(),"A",true,2000).confirmed.empty(), "two votes remain pending");
        require(c.observe(box(),"A",true,3000).confirmed=="A", "three matching votes confirm");
        require(c.observe(box(),"B",true,4000).confirmed.empty(), "conflicting latest vote returns to review");
        require(c.observe(box(),"",false,4500).confirmed.empty(), "invalid OCR cannot confirm");
        require(c.observe(box(300),"A",true,5000).observations==1, "new location clears prior target votes");
        require(c.expire(8000), "independent clock expires at TTL boundary");
        require(c.state().confirmed.empty() && !c.state().active, "expired target clears displayed state");
        require(c.observe(box(),"B",true,9000).observations==1, "next vehicle starts fresh");
        require(throws([&]{c.expire(8999);}), "backwards clock rejected");
        require(throws([]{plate::Consensus c({4,2,3000,8000,.3f});}), "nonmajority quorum rejected");
        require(throws([]{plate::Consensus c({0,0,3000,8000,.3f});}), "zero window rejected");
        plate::Consensus short_history({5,3,3000,100,.3f});
        short_history.observe(box(),"A",true,0);short_history.observe(box(),"A",true,10);
        require(short_history.observe(box(),"A",true,20).confirmed=="A", "short history confirms inside window");
        short_history.expire(121);
        require(short_history.state().confirmed.empty(), "history expiry clears old confirmation");
        std::istringstream input("1\t2f746573742e706e67\t2f6f7574\nQUIT\n"); std::ostringstream output;
        plate_stream::run(input,output,[](const plate_stream::Request& r){if(r.image!="/test.png")throw std::runtime_error("path");return 1.25;});
        require(output.str().find("@PLATE_DONE\t1\t1.250")!=std::string::npos, "stream protocol roundtrip");
        require(throws([]{plate_stream::parse("1\t00\t2f61");}), "NUL path rejected");
        std::cout << "{\"tests_passed\":" << tests << ",\"kind\":\"cpp_unit_tests\"}\n";
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
