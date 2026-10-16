#include <iostream>
#include "internal/recorded_replay.hpp"
using namespace ns3_factory::phy::internal::recorded;
int main() {
  Channel c{1000,1000,10,20,3,std::vector<Complex>(60),{},false,1,1};
  for(std::size_t t=0;t<20;++t) c.coefficients[3*t]=1.0;
  std::vector<double> x(800);
  for(std::size_t i=0;i<x.size();++i) x[i]=std::cos(2*std::numbers::pi*1000*i/8000);
  auto y=Replay(c,x,8000,100), repeated=Replay(c,x,8000,100);
  if(!y || !repeated || *y!=*repeated || y->size()!=824) return 1;
  if(Replay(c,x,8000,1900) || Replay(c,x,1000,0)) return 2;
  c.coefficients[0]={std::numeric_limits<double>::infinity(),0};
  if(Replay(c,x,8000,0)) return 3;
  c.coefficients[0]=1;
  c.delay_tracking=true;
  if(Replay(c,x,8000,0)) return 4;
  c.phase=std::vector<double>(2000,.1);
  if(!Replay(c,x,8000,100)) return 5;
  c.phase[150]=std::numeric_limits<double>::quiet_NaN();
  if(Replay(c,x,8000,100)) return 6;
  if(Resample(std::span<const Complex>{c.coefficients},0,1)) return 7;
  std::cout<<"Recorded replay repeatability, finite values and strict coverage passed\n";
}
