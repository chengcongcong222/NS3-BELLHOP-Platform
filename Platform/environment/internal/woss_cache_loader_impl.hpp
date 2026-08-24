#pragma once

#include "woss_cache_loader.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ns3_factory::environment {
namespace {

constexpr std::uintmax_t kMaximumManifestBytes = 4U * 1024U * 1024U;
constexpr std::uintmax_t kMaximumArtifactBytes = 64U * 1024U * 1024U;

template <typename T>
[[nodiscard]] auto WossFailure(contracts::ErrorCode code, std::string message)
    -> contracts::Result<T> {
  return std::unexpected(
      contracts::Error{code, std::move(message)});
}

struct JsonValue final {
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;
  using Storage =
      std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

  Storage storage;
};

class JsonParser final {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  [[nodiscard]] auto ParseDocument() -> contracts::Result<JsonValue> {
    SkipWhitespace();
    auto value = ParseValue();
    if(!value) {
      return std::unexpected(value.error());
    }
    SkipWhitespace();
    if(cursor_ != input_.size()) {
      return Error("Unexpected trailing JSON content");
    }
    return value;
  }

 private:
  [[nodiscard]] auto Error(std::string message)
      -> contracts::Result<JsonValue> {
    return WossFailure<JsonValue>(
        contracts::ErrorCode::kInvalidArgument,
        std::move(message) + " at byte " + std::to_string(cursor_));
  }

  auto SkipWhitespace() -> void {
    while(cursor_ < input_.size()) {
      const auto character = input_[cursor_];
      if(character != ' ' && character != '\t' && character != '\r' &&
         character != '\n') {
        break;
      }
      ++cursor_;
    }
  }

  [[nodiscard]] auto ParseValue() -> contracts::Result<JsonValue> {
    SkipWhitespace();
    if(cursor_ >= input_.size()) {
      return Error("Unexpected end of JSON input");
    }
    switch(input_[cursor_]) {
      case '{':
        return ParseObject();
      case '[':
        return ParseArray();
      case '"': {
        auto value = ParseString();
        if(!value) {
          return std::unexpected(value.error());
        }
        return JsonValue{std::move(*value)};
      }
      case 't':
        return ParseLiteral("true", JsonValue{true});
      case 'f':
        return ParseLiteral("false", JsonValue{false});
      case 'n':
        return ParseLiteral("null", JsonValue{nullptr});
      default:
        return ParseNumber();
    }
  }

  [[nodiscard]] auto ParseLiteral(std::string_view literal, JsonValue value)
      -> contracts::Result<JsonValue> {
    if(input_.substr(cursor_, literal.size()) != literal) {
      return Error("Invalid JSON literal");
    }
    cursor_ += literal.size();
    return value;
  }

  [[nodiscard]] auto ParseObject() -> contracts::Result<JsonValue> {
    ++cursor_;
    JsonValue::Object object;
    SkipWhitespace();
    if(cursor_ < input_.size() && input_[cursor_] == '}') {
      ++cursor_;
      return JsonValue{std::move(object)};
    }
    for(;;) {
      SkipWhitespace();
      if(cursor_ >= input_.size() || input_[cursor_] != '"') {
        return Error("JSON object key must be a string");
      }
      auto key = ParseString();
      if(!key) {
        return std::unexpected(key.error());
      }
      SkipWhitespace();
      if(cursor_ >= input_.size() || input_[cursor_] != ':') {
        return Error("JSON object key must be followed by a colon");
      }
      ++cursor_;
      auto value = ParseValue();
      if(!value) {
        return std::unexpected(value.error());
      }
      if(!object.emplace(std::move(*key), std::move(*value)).second) {
        return Error("JSON object contains a duplicate key");
      }
      SkipWhitespace();
      if(cursor_ >= input_.size()) {
        return Error("Unterminated JSON object");
      }
      const auto separator = input_[cursor_++];
      if(separator == '}') {
        return JsonValue{std::move(object)};
      }
      if(separator != ',') {
        return Error("JSON object entries must be comma-separated");
      }
    }
  }

  [[nodiscard]] auto ParseArray() -> contracts::Result<JsonValue> {
    ++cursor_;
    JsonValue::Array array;
    SkipWhitespace();
    if(cursor_ < input_.size() && input_[cursor_] == ']') {
      ++cursor_;
      return JsonValue{std::move(array)};
    }
    for(;;) {
      auto value = ParseValue();
      if(!value) {
        return std::unexpected(value.error());
      }
      array.push_back(std::move(*value));
      SkipWhitespace();
      if(cursor_ >= input_.size()) {
        return Error("Unterminated JSON array");
      }
      const auto separator = input_[cursor_++];
      if(separator == ']') {
        return JsonValue{std::move(array)};
      }
      if(separator != ',') {
        return Error("JSON array entries must be comma-separated");
      }
    }
  }

  [[nodiscard]] static auto HexDigit(char character) -> int {
    if(character >= '0' && character <= '9') {
      return character - '0';
    }
    if(character >= 'a' && character <= 'f') {
      return character - 'a' + 10;
    }
    if(character >= 'A' && character <= 'F') {
      return character - 'A' + 10;
    }
    return -1;
  }

  [[nodiscard]] auto ParseUnicodeCodeUnit()
      -> contracts::Result<std::uint32_t> {
    if(cursor_ + 4 > input_.size()) {
      return WossFailure<std::uint32_t>(
          contracts::ErrorCode::kInvalidArgument,
          "Truncated JSON unicode escape at byte " +
              std::to_string(cursor_));
    }
    std::uint32_t value = 0;
    for(std::size_t index = 0; index < 4; ++index) {
      const auto digit = HexDigit(input_[cursor_++]);
      if(digit < 0) {
        return WossFailure<std::uint32_t>(
            contracts::ErrorCode::kInvalidArgument,
            "Invalid JSON unicode escape at byte " +
                std::to_string(cursor_ - 1));
      }
      value = value * 16U + static_cast<std::uint32_t>(digit);
    }
    return value;
  }

  static auto AppendUtf8(std::string& output, std::uint32_t code_point)
      -> void {
    if(code_point <= 0x7fU) {
      output.push_back(static_cast<char>(code_point));
    } else if(code_point <= 0x7ffU) {
      output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else if(code_point <= 0xffffU) {
      output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
      output.push_back(
          static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
      output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
      output.push_back(
          static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
      output.push_back(
          static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
  }

  [[nodiscard]] auto ParseString() -> contracts::Result<std::string> {
    ++cursor_;
    std::string output;
    while(cursor_ < input_.size()) {
      const auto character = input_[cursor_++];
      if(character == '"') {
        return output;
      }
      if(static_cast<unsigned char>(character) < 0x20U) {
        return WossFailure<std::string>(
            contracts::ErrorCode::kInvalidArgument,
            "JSON string contains an unescaped control character at byte " +
                std::to_string(cursor_ - 1));
      }
      if(character != '\\') {
        output.push_back(character);
        continue;
      }
      if(cursor_ >= input_.size()) {
        return WossFailure<std::string>(
            contracts::ErrorCode::kInvalidArgument,
            "Truncated JSON escape at byte " + std::to_string(cursor_));
      }
      const auto escaped = input_[cursor_++];
      switch(escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          auto code_point = ParseUnicodeCodeUnit();
          if(!code_point) {
            return std::unexpected(code_point.error());
          }
          if(*code_point >= 0xd800U && *code_point <= 0xdbffU) {
            if(cursor_ + 2 > input_.size() || input_[cursor_] != '\\' ||
               input_[cursor_ + 1] != 'u') {
              return WossFailure<std::string>(
                  contracts::ErrorCode::kInvalidArgument,
                  "JSON high surrogate is missing its low surrogate");
            }
            cursor_ += 2;
            auto low_surrogate = ParseUnicodeCodeUnit();
            if(!low_surrogate || *low_surrogate < 0xdc00U ||
               *low_surrogate > 0xdfffU) {
              return WossFailure<std::string>(
                  contracts::ErrorCode::kInvalidArgument,
                  "Invalid JSON low surrogate");
            }
            *code_point = 0x10000U + ((*code_point - 0xd800U) << 10U) +
                          (*low_surrogate - 0xdc00U);
          } else if(*code_point >= 0xdc00U && *code_point <= 0xdfffU) {
            return WossFailure<std::string>(
                contracts::ErrorCode::kInvalidArgument,
                "Unexpected JSON low surrogate");
          }
          AppendUtf8(output, *code_point);
          break;
        }
        default:
          return WossFailure<std::string>(
              contracts::ErrorCode::kInvalidArgument,
              "Invalid JSON escape at byte " +
                  std::to_string(cursor_ - 1));
      }
    }
    return WossFailure<std::string>(contracts::ErrorCode::kInvalidArgument,
                                "Unterminated JSON string");
  }

  [[nodiscard]] auto ParseNumber() -> contracts::Result<JsonValue> {
    const auto start = cursor_;
    if(cursor_ < input_.size() && input_[cursor_] == '-') {
      ++cursor_;
    }
    if(cursor_ >= input_.size()) {
      return Error("Truncated JSON number");
    }
    if(input_[cursor_] == '0') {
      ++cursor_;
    } else if(input_[cursor_] >= '1' && input_[cursor_] <= '9') {
      while(cursor_ < input_.size() && input_[cursor_] >= '0' &&
            input_[cursor_] <= '9') {
        ++cursor_;
      }
    } else {
      return Error("Invalid JSON number");
    }
    if(cursor_ < input_.size() && input_[cursor_] == '.') {
      ++cursor_;
      const auto fraction_start = cursor_;
      while(cursor_ < input_.size() && input_[cursor_] >= '0' &&
            input_[cursor_] <= '9') {
        ++cursor_;
      }
      if(cursor_ == fraction_start) {
        return Error("JSON fraction requires digits");
      }
    }
    if(cursor_ < input_.size() &&
       (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
      ++cursor_;
      if(cursor_ < input_.size() &&
         (input_[cursor_] == '+' || input_[cursor_] == '-')) {
        ++cursor_;
      }
      const auto exponent_start = cursor_;
      while(cursor_ < input_.size() && input_[cursor_] >= '0' &&
            input_[cursor_] <= '9') {
        ++cursor_;
      }
      if(cursor_ == exponent_start) {
        return Error("JSON exponent requires digits");
      }
    }

    double value = 0.0;
    const auto token = input_.substr(start, cursor_ - start);
    const auto parsed = std::from_chars(token.data(),
                                        token.data() + token.size(),
                                        value,
                                        std::chars_format::general);
    if(parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
       !std::isfinite(value)) {
      return Error("Invalid or non-finite JSON number");
    }
    return JsonValue{value};
  }

  std::string_view input_;
  std::size_t cursor_{0};
};

[[nodiscard]] auto ReadFile(const std::filesystem::path& path,
                            std::uintmax_t maximum_bytes)
    -> contracts::Result<std::string> {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if(error) {
    return WossFailure<std::string>(contracts::ErrorCode::kNotFound,
                                "Unable to read file size: " +
                                    path.string());
  }
  if(size == 0 || size > maximum_bytes ||
     size > std::numeric_limits<std::size_t>::max()) {
    return WossFailure<std::string>(
        contracts::ErrorCode::kOutOfRange,
        "File is empty or exceeds the offline loader size limit: " +
            path.string());
  }
  std::ifstream input{path, std::ios::binary};
  if(!input) {
    return WossFailure<std::string>(contracts::ErrorCode::kUnavailable,
                                "Unable to open file: " + path.string());
  }
  std::string content(static_cast<std::size_t>(size), '\0');
  input.read(content.data(), static_cast<std::streamsize>(content.size()));
  if(!input) {
    return WossFailure<std::string>(contracts::ErrorCode::kUnavailable,
                                "Unable to read complete file: " +
                                    path.string());
  }
  return content;
}

[[nodiscard]] auto ParseJson(std::string_view content)
    -> contracts::Result<JsonValue> {
  return JsonParser{content}.ParseDocument();
}

[[nodiscard]] auto FindPath(
    const JsonValue& root,
    std::initializer_list<std::string_view> path)
    -> contracts::Result<const JsonValue*> {
  const JsonValue* current = &root;
  std::string path_text;
  for(const auto component : path) {
    if(!path_text.empty()) {
      path_text += '.';
    }
    path_text += component;
    const auto* object = std::get_if<JsonValue::Object>(&current->storage);
    if(object == nullptr) {
      return WossFailure<const JsonValue*>(
          contracts::ErrorCode::kInvalidArgument,
          "WOSS JSON field is not an object: " + path_text);
    }
    const auto found = object->find(component);
    if(found == object->end()) {
      return WossFailure<const JsonValue*>(contracts::ErrorCode::kNotFound,
                                       "Missing WOSS JSON field: " +
                                           path_text);
    }
    current = &found->second;
  }
  return current;
}

[[nodiscard]] auto RequiredString(
    const JsonValue& root,
    std::initializer_list<std::string_view> path)
    -> contracts::Result<std::string> {
  auto value = FindPath(root, path);
  if(!value) {
    return std::unexpected(value.error());
  }
  const auto* string = std::get_if<std::string>(&(*value)->storage);
  if(string == nullptr || string->empty()) {
    return WossFailure<std::string>(
        contracts::ErrorCode::kInvalidArgument,
        "WOSS JSON required string is empty or has the wrong type");
  }
  return *string;
}

[[nodiscard]] auto OptionalString(
    const JsonValue& root,
    std::initializer_list<std::string_view> path)
    -> contracts::Result<std::string> {
  auto value = FindPath(root, path);
  if(!value) {
    if(value.error().code == contracts::ErrorCode::kNotFound) {
      return std::string{};
    }
    return std::unexpected(value.error());
  }
  const auto* string = std::get_if<std::string>(&(*value)->storage);
  if(string == nullptr) {
    return WossFailure<std::string>(
        contracts::ErrorCode::kInvalidArgument,
        "WOSS JSON optional string has the wrong type");
  }
  return *string;
}

[[nodiscard]] auto RequiredNumber(
    const JsonValue& root,
    std::initializer_list<std::string_view> path)
    -> contracts::Result<double> {
  auto value = FindPath(root, path);
  if(!value) {
    return std::unexpected(value.error());
  }
  const auto* number = std::get_if<double>(&(*value)->storage);
  if(number == nullptr || !std::isfinite(*number)) {
    return WossFailure<double>(contracts::ErrorCode::kInvalidArgument,
                           "WOSS JSON required number has the wrong type");
  }
  return *number;
}

[[nodiscard]] auto RequiredNumberArray(
    const JsonValue& root,
    std::initializer_list<std::string_view> path)
    -> contracts::Result<std::vector<double>> {
  auto value = FindPath(root, path);
  if(!value) {
    return std::unexpected(value.error());
  }
  const auto* array = std::get_if<JsonValue::Array>(&(*value)->storage);
  if(array == nullptr) {
    return WossFailure<std::vector<double>>(
        contracts::ErrorCode::kInvalidArgument,
        "WOSS JSON numeric array has the wrong type");
  }
  std::vector<double> values;
  values.reserve(array->size());
  for(const auto& item : *array) {
    const auto* number = std::get_if<double>(&item.storage);
    if(number == nullptr || !std::isfinite(*number)) {
      return WossFailure<std::vector<double>>(
          contracts::ErrorCode::kInvalidArgument,
          "WOSS JSON numeric array contains a non-number");
    }
    values.push_back(*number);
  }
  return values;
}

[[nodiscard]] auto RequiredStringMap(
    const JsonValue& root,
    std::initializer_list<std::string_view> path)
    -> contracts::Result<std::map<std::string, std::string>> {
  auto value = FindPath(root, path);
  if(!value) {
    return std::unexpected(value.error());
  }
  const auto* object = std::get_if<JsonValue::Object>(&(*value)->storage);
  if(object == nullptr) {
    return WossFailure<std::map<std::string, std::string>>(
        contracts::ErrorCode::kInvalidArgument,
        "WOSS datasets field must be an object");
  }
  std::map<std::string, std::string> result;
  for(const auto& [key, item] : *object) {
    const auto* string = std::get_if<std::string>(&item.storage);
    if(string == nullptr || string->empty()) {
      return WossFailure<std::map<std::string, std::string>>(
          contracts::ErrorCode::kInvalidArgument,
          "WOSS dataset versions must be non-empty strings");
    }
    result.emplace(key, *string);
  }
  return result;
}

[[nodiscard]] auto IsWithinRoot(const std::filesystem::path& candidate,
                                const std::filesystem::path& root) -> bool {
  const auto relative = candidate.lexically_relative(root);
  if(relative.empty() || relative.is_absolute()) {
    return false;
  }
  const auto first = *relative.begin();
  return first != "..";
}

[[nodiscard]] auto ResolveArtifactPath(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& relative_path)
    -> contracts::Result<std::filesystem::path> {
  if(relative_path.empty() || relative_path.is_absolute()) {
    return WossFailure<std::filesystem::path>(
        contracts::ErrorCode::kInvalidArgument,
        "WOSS artifact path must be relative to its asset root");
  }
  for(const auto& component : relative_path) {
    if(component == "..") {
      return WossFailure<std::filesystem::path>(
          contracts::ErrorCode::kInvalidArgument,
          "WOSS artifact path must not traverse above its asset root");
    }
  }
  std::error_code error;
  auto resolved =
      std::filesystem::weakly_canonical(asset_root / relative_path, error);
  if(error || !IsWithinRoot(resolved, asset_root) ||
     !std::filesystem::is_regular_file(resolved, error) || error) {
    return WossFailure<std::filesystem::path>(
        contracts::ErrorCode::kNotFound,
        "WOSS artifact was not found under its asset root: " +
            relative_path.string());
  }
  return resolved;
}

[[nodiscard]] auto Trim(std::string_view value) -> std::string_view {
  while(!value.empty() &&
        (value.front() == ' ' || value.front() == '\t' ||
         value.front() == '\r')) {
    value.remove_prefix(1);
  }
  while(!value.empty() &&
        (value.back() == ' ' || value.back() == '\t' ||
         value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] auto ParseFiniteDouble(std::string_view text,
                                     std::string_view field_name)
    -> contracts::Result<double> {
  text = Trim(text);
  double value = 0.0;
  const auto parsed = std::from_chars(text.data(),
                                      text.data() + text.size(),
                                      value,
                                      std::chars_format::general);
  if(text.empty() || parsed.ec != std::errc{} ||
     parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
    return WossFailure<double>(contracts::ErrorCode::kInvalidArgument,
                           "Invalid finite number in " +
                               std::string{field_name});
  }
  return value;
}

[[nodiscard]] auto ParseSoundSpeedCsv(std::string_view content)
    -> contracts::Result<SoundSpeedProfile> {
  std::istringstream input{std::string{content}};
  std::string line;
  if(!std::getline(input, line)) {
    return WossFailure<SoundSpeedProfile>(contracts::ErrorCode::kInvalidArgument,
                                      "WOSS SSP CSV is empty");
  }
  if(line.starts_with("\xef\xbb\xbf")) {
    line.erase(0, 3);
  }
  if(Trim(line) != "depth_m,sound_speed_mps") {
    return WossFailure<SoundSpeedProfile>(
        contracts::ErrorCode::kInvalidArgument,
        "WOSS SSP CSV header must be depth_m,sound_speed_mps");
  }

  std::vector<SoundSpeedSample> samples;
  std::size_t line_number = 1;
  while(std::getline(input, line)) {
    ++line_number;
    if(Trim(line).empty()) {
      continue;
    }
    const auto comma = line.find(',');
    if(comma == std::string::npos ||
       line.find(',', comma + 1) != std::string::npos) {
      return WossFailure<SoundSpeedProfile>(
          contracts::ErrorCode::kInvalidArgument,
          "WOSS SSP CSV row must contain exactly two fields at line " +
              std::to_string(line_number));
    }
    auto depth = ParseFiniteDouble(
        std::string_view{line}.substr(0, comma), "SSP depth");
    auto speed = ParseFiniteDouble(
        std::string_view{line}.substr(comma + 1), "SSP sound speed");
    if(!depth) {
      return std::unexpected(depth.error());
    }
    if(!speed) {
      return std::unexpected(speed.error());
    }
    samples.push_back({*depth, *speed});
  }
  return SoundSpeedProfile::Create(std::move(samples));
}

[[nodiscard]] auto ParseBathymetryJson(std::string_view content)
    -> contracts::Result<BathymetryProfile> {
  auto json = ParseJson(content);
  if(!json) {
    return std::unexpected(json.error());
  }
  auto ranges = RequiredNumberArray(*json, {"range_m"});
  auto depths = RequiredNumberArray(*json, {"depth_m"});
  if(!ranges) {
    return std::unexpected(ranges.error());
  }
  if(!depths) {
    return std::unexpected(depths.error());
  }
  return BathymetryProfile::Create(std::move(*ranges), std::move(*depths));
}

[[nodiscard]] auto Fnv1a64Digest(std::string_view manifest,
                                 std::string_view sound_speed,
                                 std::string_view bathymetry) -> std::string {
  std::uint64_t hash = 14695981039346656037ULL;
  const auto append = [&hash](std::string_view content) {
    for(const auto character : content) {
      hash ^= static_cast<unsigned char>(character);
      hash *= 1099511628211ULL;
    }
    hash ^= 0xffU;
    hash *= 1099511628211ULL;
  };
  append(manifest);
  append(sound_speed);
  append(bathymetry);
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16)
         << hash;
  return output.str();
}

}  // namespace

inline auto WossCacheLoader::Load(const std::filesystem::path& manifest_path,
                           const std::filesystem::path& asset_root)
    -> contracts::Result<WossCachedEnvironment> {
  std::error_code error;
  const auto canonical_root =
      std::filesystem::weakly_canonical(asset_root, error);
  if(error || !std::filesystem::is_directory(canonical_root, error) || error) {
    return WossFailure<WossCachedEnvironment>(
        contracts::ErrorCode::kNotFound,
        "WOSS asset root does not exist: " + asset_root.string());
  }
  error.clear();
  const auto canonical_manifest =
      std::filesystem::weakly_canonical(manifest_path, error);
  if(error || !IsWithinRoot(canonical_manifest, canonical_root) ||
     !std::filesystem::is_regular_file(canonical_manifest, error) || error) {
    return WossFailure<WossCachedEnvironment>(
        contracts::ErrorCode::kNotFound,
        "WOSS manifest must be a file under the supplied asset root");
  }

  auto manifest_content =
      ReadFile(canonical_manifest, kMaximumManifestBytes);
  if(!manifest_content) {
    return std::unexpected(manifest_content.error());
  }
  auto manifest_json = ParseJson(*manifest_content);
  if(!manifest_json) {
    return std::unexpected(manifest_json.error());
  }

  auto source_id = RequiredString(*manifest_json, {"id"});
  auto source_name = RequiredString(*manifest_json, {"name"});
  auto provider = RequiredString(*manifest_json, {"provider"});
  auto mode = RequiredString(*manifest_json, {"mode"});
  auto sound_speed_relative =
      RequiredString(*manifest_json, {"artifacts", "ssp_file"});
  auto bathymetry_relative =
      RequiredString(*manifest_json, {"artifacts", "bathymetry_file"});
  auto latitude =
      RequiredNumber(*manifest_json, {"location", "latitude_deg"});
  auto longitude =
      RequiredNumber(*manifest_json, {"location", "longitude_deg"});
  auto time_descriptor =
      RequiredString(*manifest_json, {"time_reference", "label"});
  auto datasets = RequiredStringMap(*manifest_json, {"datasets"});
  auto source_kind =
      OptionalString(*manifest_json, {"cache", "source_kind"});
  if(!source_id || !source_name || !provider || !mode ||
     !sound_speed_relative || !bathymetry_relative || !latitude ||
     !longitude || !time_descriptor || !datasets || !source_kind) {
    const contracts::Error* errors[] = {
        source_id ? nullptr : &source_id.error(),
        source_name ? nullptr : &source_name.error(),
        provider ? nullptr : &provider.error(),
        mode ? nullptr : &mode.error(),
        sound_speed_relative ? nullptr : &sound_speed_relative.error(),
        bathymetry_relative ? nullptr : &bathymetry_relative.error(),
        latitude ? nullptr : &latitude.error(),
        longitude ? nullptr : &longitude.error(),
        time_descriptor ? nullptr : &time_descriptor.error(),
        datasets ? nullptr : &datasets.error(),
        source_kind ? nullptr : &source_kind.error()};
    for(const auto* manifest_error : errors) {
      if(manifest_error != nullptr) {
        return std::unexpected(*manifest_error);
      }
    }
  }
  if(*provider != "woss") {
    return WossFailure<WossCachedEnvironment>(
        contracts::ErrorCode::kUnsupported,
        "WOSS cache loader only accepts provider=woss manifests");
  }
  const auto has_sound_speed_source = datasets->contains("woa") ||
                                      datasets->contains("argo");
  if(!has_sound_speed_source || !datasets->contains("gebco")) {
    return WossFailure<WossCachedEnvironment>(
        contracts::ErrorCode::kFailedPrecondition,
        "WOSS manifest requires a WOA or Argo SSP source and a GEBCO "
        "bathymetry source");
  }
  if(source_kind->empty()) {
    *source_kind = *provider + ":" + *mode;
  }

  auto sound_speed_path = ResolveArtifactPath(
      canonical_root, std::filesystem::path{*sound_speed_relative});
  auto bathymetry_path = ResolveArtifactPath(
      canonical_root, std::filesystem::path{*bathymetry_relative});
  if(!sound_speed_path) {
    return std::unexpected(sound_speed_path.error());
  }
  if(!bathymetry_path) {
    return std::unexpected(bathymetry_path.error());
  }
  auto sound_speed_content = ReadFile(*sound_speed_path,
                                      kMaximumArtifactBytes);
  auto bathymetry_content = ReadFile(*bathymetry_path,
                                     kMaximumArtifactBytes);
  if(!sound_speed_content) {
    return std::unexpected(sound_speed_content.error());
  }
  if(!bathymetry_content) {
    return std::unexpected(bathymetry_content.error());
  }
  auto sound_speed_profile = ParseSoundSpeedCsv(*sound_speed_content);
  auto bathymetry_profile = ParseBathymetryJson(*bathymetry_content);
  if(!sound_speed_profile) {
    return std::unexpected(sound_speed_profile.error());
  }
  if(!bathymetry_profile) {
    return std::unexpected(bathymetry_profile.error());
  }
  auto geographic_region = GeographicRegion::Create(
      *latitude, *longitude, *latitude, *longitude);
  auto source_query = EnvironmentSourceQuery::Create(
      *latitude, *longitude, *time_descriptor);
  if(!geographic_region) {
    return std::unexpected(geographic_region.error());
  }
  if(!source_query) {
    return std::unexpected(source_query.error());
  }

  const auto digest = Fnv1a64Digest(*manifest_content,
                                    *sound_speed_content,
                                    *bathymetry_content);
  WossCacheManifest manifest{
      std::move(*source_id),
      std::move(*source_name),
      std::move(*provider),
      std::move(*mode),
      std::move(*sound_speed_path),
      std::move(*bathymetry_path),
      *latitude,
      *longitude,
      std::move(*time_descriptor),
      std::move(*datasets),
      std::move(*source_kind)};
  return WossCachedEnvironment{
      std::move(manifest),
      *geographic_region,
      std::move(*source_query),
      std::move(*sound_speed_profile),
      std::move(*bathymetry_profile),
      EnvironmentSourceProvenance{
          "woss-cache-v1",
          canonical_manifest.string(),
          digest,
          "WossCacheLoader"}};
}

}  // namespace ns3_factory::environment
