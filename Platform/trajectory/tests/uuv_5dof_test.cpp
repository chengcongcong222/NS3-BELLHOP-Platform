#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <ns3_factory/trajectory/uuv_5dof.hpp>
using namespace ns3_factory::trajectory;
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class T> T Take(Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
int main(){try{
  Uuv5DofPlant plant;
  DirectUuvController zero{{}};
  auto still=Take(GenerateUuvTrajectory("still",1,"N1",2,0.1,{},plant,zero));
  Check(still.asset.Query("N1",SimTime::FromNanoseconds(1'000'000'000))->position==Position3d{0,0,0},"zero input");
  DirectUuvController thrust{{120,0,0,0,0}};
  auto moving=Take(GenerateUuvTrajectory("moving",1,"N1",2,0.1,{},plant,thrust));
  Check(moving.samples.back().ned.north_m>0 && moving.samples.back().ned.surge_mps>0,"surge trend");
  Repository repository; Check(repository.Register(moving.asset).has_value(),"generated asset registration");
  auto bound=Take(BoundTrajectories::Create(repository,{{NodeId{7},{"moving",1,"N1"}}}));
  Check(Take(bound->Query(NodeId{7},SimTime::FromNanoseconds(1'000'000'000))).position.x_meters>0,
        "generated asset binds runtime trajectory query");
  auto repeat=Take(GenerateUuvTrajectory("moving",1,"N1",2,0.1,{},plant,thrust));
  Check(moving.asset.checksum()==repeat.asset.checksum() && moving.samples==repeat.samples,"RK4 deterministic");
  UuvState down{}; down.down_m=12;
  auto coordinates=Take(GenerateUuvTrajectory("frame",1,"N1",1,0.1,down,plant,zero));
  Check(coordinates.samples.front().platform_position==Position3d{0,0,-12},"NED z conversion");
  WaypointUuvController turn(1.0,{{0,100,0}});
  auto turning=Take(GenerateUuvTrajectory("turn",1,"N1",4,0.1,{},plant,turn));
  Check(turning.samples.back().ned.yaw_rad>0,"heading control");
  WaypointUuvController dive(1.0,{{100,0,10}});
  auto diving=Take(GenerateUuvTrajectory("dive",1,"N1",4,0.1,{},plant,dive));
  Check(diving.samples.back().ned.down_m>0 && diving.samples.back().platform_position.z_meters<0,"depth control");
  Check(!GenerateUuvTrajectory("bad",1,"N1",1,0.3,{},plant,zero),"non integral fixed step rejected");
  std::cout<<"UUV 5DOF plant, controller, RK4 and coordinate conversion PASS\n";
  return EXIT_SUCCESS;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return EXIT_FAILURE;}}
