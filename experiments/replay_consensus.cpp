#include "../plate_persistent/plate_consensus.h"
#include <iostream>
#include <sstream>
int main(int argc,char** argv) {
    try {
        plate::ConsensusConfig config;
        if(argc>1)config.window=std::stoul(argv[1]);
        config.votes=config.window/2+1;
        plate::Consensus consensus(config);
        std::string line,raw;long long time;float x,y,w,h;int valid;
        while(std::getline(std::cin,line)) {
            if(line.empty())continue;
            std::istringstream stream(line);
            if(!(stream>>time>>x>>y>>w>>h>>valid>>raw)) throw std::runtime_error("Bad replay input");
            if(raw=="-")raw.clear();
            plate::ConsensusState state;
            if(valid<0) {consensus.reset();consensus.expire(time);state=consensus.state();}
            else if(w<=0 || h<=0) {consensus.expire(time);state=consensus.state();}
            else {plate::Detection d;d.box={x,y,x+w,y+h};state=consensus.observe(d,raw,valid!=0,time);}
            std::cout<<time<<'\t'<<(state.raw.empty()?"-":state.raw)<<'\t'
                     <<(state.confirmed.empty()?"-":state.confirmed)<<'\t'<<state.support<<'\n';
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
