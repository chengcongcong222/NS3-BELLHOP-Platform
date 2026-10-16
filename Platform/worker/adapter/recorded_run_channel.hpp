#pragma once
#include <bit>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "planning_context_json.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/recorded_waveform_channel.hpp"

namespace ns3_factory::worker::adapter {
inline auto LoadRecordedRunChannel(const std::filesystem::path& authoring_root,
    const nlohmann::json& config,double baseband_rate)
    -> contracts::Result<std::shared_ptr<const phy::internal::RecordedWaveformChannel>> {
  using namespace contracts;
  using namespace phy::internal;
  try {
    const auto& m=config.at("manifest");
    const auto id=m.at("asset_id").get<std::string>();
    if(!environment::internal::EnvironmentAssetId::Create(id)||m.at("schema")!="ns3-factory-recorded-channel-v1"||
       m.at("version")!=1||m.at("gain_reference")!="Normalized"||m.at("geometry")!="Unspecified"||
       m.at("reference_revision")!="dbf373ca7cd07ac4eb1b24a191d76bea18f73560"||
       !std::isfinite(baseband_rate)||baseband_rate<=0||baseband_rate!=std::floor(baseband_rate)||baseband_rate>1'000'000)
      throw std::invalid_argument("Invalid recorded source identity or waveform clock");
    const auto directory=authoring_root/"recorded-channels"/id/"1";
    if(std::filesystem::is_symlink(directory)||std::filesystem::is_symlink(directory.parent_path())||
       std::filesystem::is_symlink(directory/"manifest.json"))throw std::invalid_argument("Linked recorded resource rejected");
    std::ifstream manifest_file{directory/"manifest.json"};
    if(!manifest_file||nlohmann::json::parse(manifest_file)!=m)
      throw std::invalid_argument("Recorded asset manifest differs from frozen definition");
    auto read=[&](const std::filesystem::path& path,std::size_t expected,const nlohmann::json& info)->std::vector<double> {
      if(expected>40'000'000)throw std::invalid_argument("Recorded binary budget exceeded");
      if(std::filesystem::is_symlink(path)||std::filesystem::file_size(path)!=expected*sizeof(double)||
         info.at("bytes")!=expected*sizeof(double))
        throw std::invalid_argument("Recorded binary size or file kind mismatch");
      std::ifstream input{path,std::ios::binary};
      std::vector<std::uint64_t> raw(expected);
      if(!input.read(reinterpret_cast<char*>(raw.data()),static_cast<std::streamsize>(raw.size()*8)))
        throw std::invalid_argument("Recorded binary read failed");
      std::uint64_t fnv=14695981039346656037ULL;
      const auto* bytes=reinterpret_cast<const unsigned char*>(raw.data());
      for(std::size_t i=0;i<raw.size()*8;++i)fnv=(fnv^bytes[i])*1099511628211ULL;
      std::ostringstream hash;hash<<std::hex<<std::setw(16)<<std::setfill('0')<<fnv;
      if(hash.str()!=info.at("fnv1a64").get<std::string>())
        throw std::invalid_argument("Recorded binary checksum mismatch");
      std::vector<double> values;values.reserve(expected);
      for(auto word:raw) {
        if constexpr(std::endian::native!=std::endian::little)word=std::byteswap(word);
        const auto value=std::bit_cast<double>(word);
        if(!std::isfinite(value))throw std::invalid_argument("Non-finite recorded sample");
        values.push_back(value);
      }
      return values;
    };
    const auto snapshots=m.at("snapshots").get<std::size_t>(),taps=m.at("delay_taps").get<std::size_t>();
    if(snapshots<2||!taps||snapshots>20'000'000/taps)throw std::invalid_argument("Invalid recorded tensor size");
    auto values=read(directory/"channel.f64le",snapshots*taps*2,m.at("files").at("channel.f64le"));
    auto c=std::make_shared<recorded::Channel>(recorded::Channel{
        m.at("fc"),m.at("fs_delay"),m.at("fs_time"),snapshots,taps,{},
        read(directory/"phase.f64le",m.at("phase_count"),m.at("files").at("phase.f64le")),m.at("tracking")=="phi_hat",m.at("resample_up"),m.at("resample_down")});
    c->coefficients.reserve(snapshots*taps);
    for(std::size_t i=0;i<values.size();i+=2)c->coefficients.emplace_back(values[i],values[i+1]);
    std::shared_ptr<const std::vector<double>> noise;
    if(config.at("noise").at("mode")=="ContinuousLibrary") {
      const auto& nm=config.at("noise").at("manifest");
      const auto noise_id=nm.at("asset_id").get<std::string>();
      if(!environment::internal::EnvironmentAssetId::Create(noise_id)||nm.at("schema")!="recorded-continuous-noise-v1"||
         nm.at("version")!=1||nm.at("sample_rate_hz")!=std::ceil(4*c->carrier_hz)||
         nm.at("center_frequency_hz")!=m.at("fc")||nm.at("shape").at(1)!=1||
         nm.at("elements")!=nlohmann::json::array({m.at("element")})||
         nm.at("reference_noisegen_sha256")!="11f09a3b51ee10abd0fef8f4276e0b6a246cd9de705fd3e249212e0cb3966550")
        throw std::invalid_argument("Continuous noise metadata does not match recording");
      const auto nd=authoring_root/"recorded-noise"/noise_id/"1";
      if(std::filesystem::is_symlink(nd)||std::filesystem::is_symlink(nd.parent_path())||
         std::filesystem::is_symlink(nd/"manifest.json"))throw std::invalid_argument("Linked noise rejected");
      std::ifstream nf{nd/"manifest.json"};
      if(!nf||nlohmann::json::parse(nf)!=nm)throw std::invalid_argument("Frozen noise manifest mismatch");
      noise=std::make_shared<const std::vector<double>>(read(nd/"noise.f64le",nm.at("shape").at(0),
          nlohmann::json{{"bytes",nm.at("bytes")},{"fnv1a64",nm.at("samples_fnv1a64")}}));
    } else if(config.at("noise")!=nlohmann::json{{"mode","None"}})
      throw std::invalid_argument("Unknown recorded noise mode");
    auto number=[&](std::string_view key){return planning_json::Unsigned(config.at(key));};
    const auto delay=number("modeled_reference_delay_ns");
    if(delay>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      throw std::invalid_argument("Recorded modeled delay overflow");
    auto channel=RecordedWaveformChannel::Create({RecordedLink{
        NodeId{number("transmitter_node_id")},NodeId{number("receiver_node_id")},c,number("start_sample"),
        SimDuration::FromNanoseconds(delay),config.at("relative_amplitude"),static_cast<std::size_t>(baseband_rate),
        static_cast<std::size_t>(std::ceil(4*c->carrier_hz)),m.at("usable_bandwidth_hz"),std::move(noise)}});
    if(!channel)return std::unexpected(channel.error());
    return std::make_shared<const RecordedWaveformChannel>(std::move(*channel));
  } catch(const std::exception& error) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,std::string{"Recorded resource unavailable: "}+error.what()});
  }
}
} // namespace ns3_factory::worker::adapter
