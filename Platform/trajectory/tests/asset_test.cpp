#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <ns3_factory/trajectory/csv_import.hpp>
#include <ns3_factory/trajectory/generator.hpp>

using namespace ns3_factory::trajectory;
constexpr auto T(std::int64_t seconds) { return SimTime::FromNanoseconds(seconds*1'000'000'000); }
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class T> auto Take(Result<T> value) -> T {
  if(!value) throw std::runtime_error(value.error().message);
  return std::move(*value);
}
int main() {
  try {
    constexpr auto frame = CoordinateSystem::kLocalCartesianMetersZUp;
    auto asset = Take(Asset::Create("mission",1,frame,T(0),T(20),"external-test",
        {{"moving",{{T(0),{0,0,-60}},{T(10),{10,20,-50}},{T(20),{0,40,-60}}}},
         {"fixed",{{T(0),{100,0,-8}}}}}));
    Check(Take(asset.Query("moving",T(10))).position == Position3d{10,20,-50},"exact knot");
    Check(Take(asset.Query("moving",T(5))).position == Position3d{5,10,-55},"interpolation");
    Check(Take(asset.Query("moving",T(20))).position == Position3d{0,40,-60},"final endpoint");
    Check(Take(asset.Query("fixed",T(17))).position == Position3d{100,0,-8},"fixed track");
    Check(!asset.Query("moving",T(-1)) && !asset.Query("moving",T(21)),"out of bounds rejected");
    Check(!asset.Query("missing",T(1)),"unknown track");
    for(int i=0;i<100;++i) Check(Take(asset.Query("moving",T(5))) == Take(asset.Query("moving",T(5))),"repeatability");
    Repository repository;
    Check(repository.Register(asset).has_value(),"registration");
    Check(!repository.Register(asset) && !repository.Load("mission",2),"exact immutable version");
    auto binding = Take(BoundTrajectories::Create(repository,
        {{NodeId{0},{"mission",1,"moving"}},{NodeId{1},{"mission",1,"fixed"}},
         {NodeId{2},{"mission",1,"moving"}}}));
    Check(Take(binding->Query(NodeId{0},T(5))) == Take(binding->Query(NodeId{2},T(5))),"shared track");
    Check(!binding->Query(NodeId{3},T(5)),"missing node");
    Check(!BoundTrajectories::Create(repository,{{NodeId{0},{"mission",1,"fixed"}},
                                                 {NodeId{0},{"mission",1,"moving"}}}),"duplicate binding");
    auto invalid = [&](std::vector<Point> points) {
      return !Asset::Create("bad",1,frame,T(0),T(20),"test",{{"N",std::move(points)}});
    };
    Check(invalid({{T(0),{0,0,0}},{T(10),{1,0,0}},{T(10),{2,0,0}},{T(20),{3,0,0}}}),"duplicate time");
    Check(invalid({{T(0),{0,0,0}},{T(15),{1,0,0}},{T(10),{2,0,0}},{T(20),{3,0,0}}}),"unordered time");
    Check(invalid({{T(0),{std::numeric_limits<double>::infinity(),0,0}}}),"nonfinite");
    Check(!Asset::Create("bad",1,static_cast<CoordinateSystem>(2),T(0),T(20),"test",{{"N",{{T(0),{0,0,0}}}}}),"coordinate frame");
    const std::string csv = "track_id,time_s,x_m,y_m,z_m\nN10,20,1027.776,0,-60\nN10,0,1000,0,-60\nN10,10,1013.888,0,-60\n";
    auto imported = Take(ImportCsv(csv,"csv",1,frame,"external-csv-v1"));
    Check(std::abs(Take(imported.Query("N10",T(5))).position.x_meters-1006.944)<1e-10,"CSV interpolation");
    auto ordered = Take(ImportCsv("track_id,time_s,x_m,y_m,z_m\nN10,0,1000,0,-60\nN10,10,1013.888,0,-60\nN10,20,1027.776,0,-60\n","csv",1,frame,"external-csv-v1"));
    Check(imported.checksum() == ordered.checksum(),"canonical checksum independent of CSV order");
    for(const auto row : {"N,0,nan,0,0", "N,-1,0,0,0", "N,0.0000000001,0,0,0",
                          "N,0,0,0,0,extra", "N,0,0,0", "N,0,0,0,0\nN,0,1,0,0"})
      Check(!ImportCsv(std::string("track_id,time_s,x_m,y_m,z_m\n")+row,"csv",1,frame,"test"),"bad CSV rejected");
    Check(Take(ParseSeconds("0.000000001")).nanoseconds() == 1,"exact ns");
    Check(Take(ParseSeconds("9223372036.854775807")).nanoseconds() == std::numeric_limits<std::int64_t>::max(),"max ns");
    Check(!ParseSeconds("9223372036.854775808") && !ParseSeconds("1e2"),"overflow and exponent rejection");
    std::cout << "trajectory asset, interpolation, fixed/shared tracks, validation, CSV and determinism PASS\n";
    return EXIT_SUCCESS;
  } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return EXIT_FAILURE; }
}
