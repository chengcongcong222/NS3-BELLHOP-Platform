#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::adapters::internal {

enum class ExternalCirScalarEncoding : std::uint8_t {
  kFloat32 = 1,
  kFloat64 = 2,
};

enum class ExternalCirByteOrder : std::uint8_t {
  kLittleEndian = 1,
  kBigEndian = 2,
};

struct ExternalCirReceiverBinding final {
  contracts::NodeId receiver_node_id;
  std::size_t receiver_index;

  auto operator==(const ExternalCirReceiverBinding&) const -> bool = default;
};

// The headerless binary tensor order is
// [snapshot][receiver][delay][I,Q]. Every interpretation choice is explicit in
// this descriptor; the adapter never guesses an external format.
struct ExternalCirDescriptor final {
  std::uint32_t format_version;
  std::filesystem::path data_file;
  ExternalCirScalarEncoding scalar_encoding;
  ExternalCirByteOrder byte_order;
  std::size_t delay_bin_count;
  std::size_t receiver_count;
  std::size_t snapshot_count;
  double delay_sample_rate_hz;
  double snapshot_rate_hz;
  double center_frequency_hz;
  double bandwidth_hz;
  double center_frequency_tolerance_hz;
  contracts::SimDuration cir_origin_delay;
  contracts::SimTime snapshot_origin;
  double minimum_tap_magnitude;
  contracts::NodeId sender_node_id;
  std::vector<ExternalCirReceiverBinding> receiver_bindings;
};

namespace external_cir_detail {

using contracts::Error;
using contracts::ErrorCode;
using contracts::Result;

[[nodiscard]] inline auto Invalid(std::string message) -> Error {
  return Error{ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] inline auto CheckedMultiply(std::size_t lhs,
                                          std::size_t rhs)
    -> Result<std::size_t> {
  if(lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "External CIR tensor size overflow"});
  }
  return lhs * rhs;
}

[[nodiscard]] inline auto ScalarBytes(ExternalCirScalarEncoding encoding)
    -> Result<std::size_t> {
  switch(encoding) {
    case ExternalCirScalarEncoding::kFloat32:
      return sizeof(float);
    case ExternalCirScalarEncoding::kFloat64:
      return sizeof(double);
  }
  return std::unexpected(Invalid("External CIR scalar encoding is invalid"));
}

[[nodiscard]] inline auto ValidateDescriptor(
    const ExternalCirDescriptor& descriptor) -> contracts::Status {
  if(descriptor.format_version != 1U) {
    return std::unexpected(
        Error{ErrorCode::kUnsupported,
              "Only normalized external CIR format version 1 is supported"});
  }
  if(descriptor.data_file.empty() || descriptor.delay_bin_count == 0U ||
     descriptor.receiver_count == 0U || descriptor.snapshot_count == 0U ||
     descriptor.receiver_bindings.empty()) {
    return std::unexpected(
        Invalid("External CIR dimensions, bindings, and data file are required"));
  }
  if(!ScalarBytes(descriptor.scalar_encoding)) {
    return std::unexpected(Invalid("External CIR scalar encoding is invalid"));
  }
  if(descriptor.byte_order != ExternalCirByteOrder::kLittleEndian &&
     descriptor.byte_order != ExternalCirByteOrder::kBigEndian) {
    return std::unexpected(Invalid("External CIR byte order is invalid"));
  }
  if(!std::isfinite(descriptor.delay_sample_rate_hz) ||
     !std::isfinite(descriptor.snapshot_rate_hz) ||
     !std::isfinite(descriptor.center_frequency_hz) ||
     !std::isfinite(descriptor.bandwidth_hz) ||
     !std::isfinite(descriptor.center_frequency_tolerance_hz) ||
     !std::isfinite(descriptor.minimum_tap_magnitude) ||
     descriptor.delay_sample_rate_hz <= 0.0 ||
     descriptor.snapshot_rate_hz <= 0.0 ||
     descriptor.center_frequency_hz <= 0.0 ||
     descriptor.bandwidth_hz <= 0.0 ||
     descriptor.bandwidth_hz / 2.0 >= descriptor.center_frequency_hz ||
     descriptor.center_frequency_tolerance_hz < 0.0 ||
     descriptor.minimum_tap_magnitude < 0.0 ||
     descriptor.cir_origin_delay < contracts::SimDuration::Zero()) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "External CIR rates, band, delay, tolerance, and threshold "
              "must be finite and physically valid"});
  }

  auto bindings = descriptor.receiver_bindings;
  std::sort(bindings.begin(), bindings.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.receiver_node_id < rhs.receiver_node_id;
            });
  for(std::size_t index = 0U; index < bindings.size(); ++index) {
    if(bindings[index].receiver_index >= descriptor.receiver_count) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange,
                "External CIR receiver binding index is out of range"});
    }
    if(index != 0U &&
       bindings[index - 1U].receiver_node_id ==
           bindings[index].receiver_node_id) {
      return std::unexpected(
          Invalid("External CIR receiver node bindings must be unique"));
    }
  }
  return {};
}

[[nodiscard]] inline auto ParseScalarEncoding(const std::string& value)
    -> Result<ExternalCirScalarEncoding> {
  if(value == "float32") return ExternalCirScalarEncoding::kFloat32;
  if(value == "float64") return ExternalCirScalarEncoding::kFloat64;
  return std::unexpected(
      Error{ErrorCode::kUnsupported,
            "External CIR scalar_encoding must be float32 or float64"});
}

[[nodiscard]] inline auto ParseByteOrder(const std::string& value)
    -> Result<ExternalCirByteOrder> {
  if(value == "little") return ExternalCirByteOrder::kLittleEndian;
  if(value == "big") return ExternalCirByteOrder::kBigEndian;
  return std::unexpected(
      Error{ErrorCode::kUnsupported,
            "External CIR byte_order must be little or big"});
}

[[nodiscard]] inline auto LoadDescriptor(
    const std::filesystem::path& descriptor_file)
    -> Result<ExternalCirDescriptor> {
  std::ifstream input{descriptor_file};
  if(!input) {
    return std::unexpected(
        Error{ErrorCode::kNotFound,
              "External CIR descriptor file was not found"});
  }

  try {
    const auto json = nlohmann::json::parse(input);
    constexpr std::array required{
        "format_version",
        "data_file",
        "scalar_encoding",
        "byte_order",
        "complex_layout",
        "tensor_layout",
        "delay_bin_count",
        "receiver_count",
        "snapshot_count",
        "delay_sample_rate_hz",
        "snapshot_rate_hz",
        "center_frequency_hz",
        "bandwidth_hz",
        "center_frequency_tolerance_hz",
        "cir_origin_delay_ns",
        "snapshot_origin_ns",
        "minimum_tap_magnitude",
        "sender_node_id",
        "receiver_bindings"};
    for(const auto* key : required) {
      if(!json.contains(key)) {
        return std::unexpected(
            Invalid(std::string{"External CIR descriptor is missing: "} + key));
      }
    }
    if(json.at("complex_layout").get<std::string>() != "iq_interleaved" ||
       json.at("tensor_layout").get<std::string>() !=
           "snapshot_receiver_delay") {
      return std::unexpected(
          Error{ErrorCode::kUnsupported,
                "External CIR requires iq_interleaved samples in "
                "snapshot_receiver_delay tensor order"});
    }

    auto scalar =
        ParseScalarEncoding(json.at("scalar_encoding").get<std::string>());
    auto byte_order =
        ParseByteOrder(json.at("byte_order").get<std::string>());
    if(!scalar) return std::unexpected(scalar.error());
    if(!byte_order) return std::unexpected(byte_order.error());

    std::vector<ExternalCirReceiverBinding> bindings;
    const auto& binding_json = json.at("receiver_bindings");
    if(!binding_json.is_array()) {
      return std::unexpected(
          Invalid("External CIR receiver_bindings must be an array"));
    }
    bindings.reserve(binding_json.size());
    for(const auto& value : binding_json) {
      bindings.push_back(ExternalCirReceiverBinding{
          contracts::NodeId{value.at("node_id").get<std::uint64_t>()},
          value.at("receiver_index").get<std::size_t>()});
    }

    auto data_file =
        std::filesystem::path{json.at("data_file").get<std::string>()};
    if(data_file.is_relative()) {
      data_file = descriptor_file.parent_path() / data_file;
    }
    ExternalCirDescriptor descriptor{
        json.at("format_version").get<std::uint32_t>(),
        std::move(data_file),
        *scalar,
        *byte_order,
        json.at("delay_bin_count").get<std::size_t>(),
        json.at("receiver_count").get<std::size_t>(),
        json.at("snapshot_count").get<std::size_t>(),
        json.at("delay_sample_rate_hz").get<double>(),
        json.at("snapshot_rate_hz").get<double>(),
        json.at("center_frequency_hz").get<double>(),
        json.at("bandwidth_hz").get<double>(),
        json.at("center_frequency_tolerance_hz").get<double>(),
        contracts::SimDuration::FromNanoseconds(
            json.at("cir_origin_delay_ns")
                .get<contracts::NanosecondCount>()),
        contracts::SimTime::FromNanoseconds(
            json.at("snapshot_origin_ns")
                .get<contracts::NanosecondCount>()),
        json.at("minimum_tap_magnitude").get<double>(),
        contracts::NodeId{
            json.at("sender_node_id").get<std::uint64_t>()},
        std::move(bindings)};
    auto valid = ValidateDescriptor(descriptor);
    if(!valid) return std::unexpected(valid.error());
    return descriptor;
  } catch(const std::exception& exception) {
    return std::unexpected(
        Invalid(std::string{"Invalid external CIR descriptor JSON: "} +
                exception.what()));
  }
}

template <typename Scalar>
[[nodiscard]] inline auto DecodeScalar(
    std::span<const std::byte, sizeof(Scalar)> bytes,
    ExternalCirByteOrder byte_order) -> Scalar {
  std::array<std::byte, sizeof(Scalar)> value{};
  std::copy(bytes.begin(), bytes.end(), value.begin());
  const auto file_is_little =
      byte_order == ExternalCirByteOrder::kLittleEndian;
  const auto native_is_little = std::endian::native == std::endian::little;
  if(file_is_little != native_is_little) {
    std::reverse(value.begin(), value.end());
  }
  return std::bit_cast<Scalar>(value);
}

[[nodiscard]] inline auto LoadSamples(const ExternalCirDescriptor& descriptor)
    -> Result<std::vector<std::complex<double>>> {
  auto scalar_count = CheckedMultiply(descriptor.snapshot_count,
                                      descriptor.receiver_count);
  if(!scalar_count) return std::unexpected(scalar_count.error());
  scalar_count = CheckedMultiply(*scalar_count, descriptor.delay_bin_count);
  if(!scalar_count) return std::unexpected(scalar_count.error());
  scalar_count = CheckedMultiply(*scalar_count, 2U);
  if(!scalar_count) return std::unexpected(scalar_count.error());
  const auto scalar_bytes = ScalarBytes(descriptor.scalar_encoding);
  if(!scalar_bytes) return std::unexpected(scalar_bytes.error());
  const auto expected_bytes = CheckedMultiply(*scalar_count, *scalar_bytes);
  if(!expected_bytes) return std::unexpected(expected_bytes.error());

  std::ifstream input{descriptor.data_file, std::ios::binary | std::ios::ate};
  if(!input) {
    return std::unexpected(
        Error{ErrorCode::kNotFound,
              "External CIR binary data file was not found"});
  }
  const auto end = input.tellg();
  if(end < 0 || static_cast<std::uintmax_t>(end) != *expected_bytes) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "External CIR binary size does not match its descriptor"});
  }
  input.seekg(0, std::ios::beg);
  std::vector<std::byte> bytes(*expected_bytes);
  if(!input.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()))) {
    return std::unexpected(
        Error{ErrorCode::kUnavailable,
              "External CIR binary data could not be read completely"});
  }

  std::vector<std::complex<double>> samples;
  samples.reserve(*scalar_count / 2U);
  std::size_t offset = 0U;
  while(offset < bytes.size()) {
    double real = 0.0;
    double imaginary = 0.0;
    if(descriptor.scalar_encoding == ExternalCirScalarEncoding::kFloat32) {
      const auto first = std::span<const std::byte, sizeof(float)>{
          bytes.data() + offset, sizeof(float)};
      offset += sizeof(float);
      const auto second = std::span<const std::byte, sizeof(float)>{
          bytes.data() + offset, sizeof(float)};
      offset += sizeof(float);
      real = DecodeScalar<float>(first, descriptor.byte_order);
      imaginary = DecodeScalar<float>(second, descriptor.byte_order);
    } else {
      const auto first = std::span<const std::byte, sizeof(double)>{
          bytes.data() + offset, sizeof(double)};
      offset += sizeof(double);
      const auto second = std::span<const std::byte, sizeof(double)>{
          bytes.data() + offset, sizeof(double)};
      offset += sizeof(double);
      real = DecodeScalar<double>(first, descriptor.byte_order);
      imaginary = DecodeScalar<double>(second, descriptor.byte_order);
    }
    if(!std::isfinite(real) || !std::isfinite(imaginary)) {
      return std::unexpected(Invalid("External CIR samples must be finite"));
    }
    samples.emplace_back(real, imaginary);
  }
  return samples;
}

[[nodiscard]] inline auto SecondsToNanoseconds(long double seconds)
    -> Result<contracts::NanosecondCount> {
  const auto nanoseconds = std::round(seconds * 1.0e9L);
  if(!std::isfinite(nanoseconds) || nanoseconds < 0.0L ||
     nanoseconds >
         static_cast<long double>(
             std::numeric_limits<contracts::NanosecondCount>::max())) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "External CIR delay exceeds SimDuration range"});
  }
  return static_cast<contracts::NanosecondCount>(nanoseconds);
}

}  // namespace external_cir_detail

class ExternalCirChannelProvider final
    : public contracts::IChannelFieldProvider {
 public:
  [[nodiscard]] static auto Create(
      const std::filesystem::path& descriptor_file)
      -> contracts::Result<ExternalCirChannelProvider> {
    auto descriptor =
        external_cir_detail::LoadDescriptor(descriptor_file);
    if(!descriptor) return std::unexpected(descriptor.error());
    auto samples = external_cir_detail::LoadSamples(*descriptor);
    if(!samples) return std::unexpected(samples.error());
    return ExternalCirChannelProvider{std::move(*descriptor),
                                      std::move(*samples)};
  }

  [[nodiscard]] auto Query(const contracts::ChannelQuery& query) const
      -> contracts::Result<contracts::ChannelFieldOutcome> override {
    if(query.sender_node_id() != descriptor_.sender_node_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "External CIR has no binding for the sender"});
    }
    const auto receiver = std::find_if(
        descriptor_.receiver_bindings.begin(),
        descriptor_.receiver_bindings.end(),
        [&query](const auto& binding) {
          return binding.receiver_node_id == query.receiver_node_id();
        });
    if(receiver == descriptor_.receiver_bindings.end()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "External CIR has no binding for the receiver"});
    }
    if(std::abs(query.center_frequency_hz() -
                descriptor_.center_frequency_hz) >
           descriptor_.center_frequency_tolerance_hz ||
       query.bandwidth_hz() > descriptor_.bandwidth_hz) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Channel query is outside external CIR frequency "
                           "or bandwidth coverage"});
    }

    const auto elapsed = contracts::CheckedSubtract(
        query.emitted_at(), descriptor_.snapshot_origin);
    if(!elapsed || *elapsed < contracts::SimDuration::Zero()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Channel query precedes external CIR time coverage"});
    }
    std::size_t snapshot_index = 0U;
    if(descriptor_.snapshot_count > 1U) {
      const auto position =
          static_cast<long double>(elapsed->nanoseconds()) *
          static_cast<long double>(descriptor_.snapshot_rate_hz) / 1.0e9L;
      if(!std::isfinite(position) || position < 0.0L ||
         position >=
             static_cast<long double>(descriptor_.snapshot_count)) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kOutOfRange,
                             "Channel query exceeds external CIR time coverage"});
      }
      snapshot_index = static_cast<std::size_t>(std::floor(position));
    }

    const auto slice_offset =
        (snapshot_index * descriptor_.receiver_count +
         receiver->receiver_index) *
        descriptor_.delay_bin_count;
    const auto slice = std::span<const std::complex<double>>{
        samples_.data() + slice_offset, descriptor_.delay_bin_count};
    auto first_active = slice.size();
    for(std::size_t index = 0U; index < slice.size(); ++index) {
      if(std::abs(slice[index]) > descriptor_.minimum_tap_magnitude) {
        first_active = index;
        break;
      }
    }
    if(first_active == slice.size()) {
      contracts::ChannelFieldOutcome outcome{contracts::ChannelNoArrival{
          query.transmission_id(), query.receiver_node_id()}};
      const auto identity =
          contracts::ValidateChannelFieldOutcomeIdentity(query, outcome);
      if(!identity) return std::unexpected(identity.error());
      return outcome;
    }

    const auto first_offset_ns =
        external_cir_detail::SecondsToNanoseconds(
            static_cast<long double>(first_active) /
            static_cast<long double>(descriptor_.delay_sample_rate_hz));
    if(!first_offset_ns) return std::unexpected(first_offset_ns.error());
    const auto first_arrival = contracts::CheckedAdd(
        descriptor_.cir_origin_delay,
        contracts::SimDuration::FromNanoseconds(*first_offset_ns));
    if(!first_arrival) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "External CIR first-arrival delay overflowed"});
    }

    std::vector<contracts::PropagationPath> paths;
    long double total_power = 0.0L;
    for(std::size_t index = first_active; index < slice.size(); ++index) {
      const auto gain = slice[index];
      const auto magnitude = std::abs(gain);
      if(magnitude <= descriptor_.minimum_tap_magnitude) continue;
      const auto excess_seconds =
          static_cast<long double>(index - first_active) /
          static_cast<long double>(descriptor_.delay_sample_rate_hz);
      const auto excess_ns =
          external_cir_detail::SecondsToNanoseconds(excess_seconds);
      if(!excess_ns) return std::unexpected(excess_ns.error());

      // Imported coefficients already carry complex-baseband phase. The
      // waveform adapter later subtracts carrier phase for a relative delay,
      // so encode the inverse compensation here. The final tap then equals h.
      const auto carrier_phase =
          2.0L * std::numbers::pi_v<long double> *
          static_cast<long double>(query.center_frequency_hz()) *
          excess_seconds;
      const auto provider_phase = std::remainder(
          static_cast<long double>(std::arg(gain)) + carrier_phase,
          2.0L * std::numbers::pi_v<long double>);
      auto path = contracts::PropagationPath::Create(
          contracts::SimDuration::FromNanoseconds(*excess_ns),
          magnitude,
          static_cast<double>(provider_phase));
      if(!path) return std::unexpected(path.error());
      paths.push_back(std::move(*path));
      total_power += static_cast<long double>(std::norm(gain));
    }
    if(paths.empty() || total_power <= 0.0L ||
       !std::isfinite(total_power)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInternal,
                           "External CIR active-path power is invalid"});
    }

    const auto aggregate_loss_db =
        static_cast<double>(-10.0L * std::log10(total_power));
    auto response = contracts::ChannelFieldResponse::Create(
        query.transmission_id(),
        query.receiver_node_id(),
        aggregate_loss_db,
        *first_arrival,
        std::move(paths));
    if(!response) return std::unexpected(response.error());
    contracts::ChannelFieldOutcome outcome{std::move(*response)};
    const auto identity =
        contracts::ValidateChannelFieldOutcomeIdentity(query, outcome);
    if(!identity) return std::unexpected(identity.error());
    return outcome;
  }

  [[nodiscard]] constexpr auto descriptor() const noexcept
      -> const ExternalCirDescriptor& {
    return descriptor_;
  }

 private:
  ExternalCirChannelProvider(
      ExternalCirDescriptor descriptor,
      std::vector<std::complex<double>> samples)
      : descriptor_(std::move(descriptor)),
        samples_(std::move(samples)) {}

  ExternalCirDescriptor descriptor_;
  std::vector<std::complex<double>> samples_;
};

}  // namespace ns3_factory::adapters::internal
