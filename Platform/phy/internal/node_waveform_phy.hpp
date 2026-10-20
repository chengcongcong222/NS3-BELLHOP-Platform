#pragma once
#include <map>
#include "waveform_phy.hpp"

namespace ns3_factory::phy::internal {
// Explicit, frozen modem knowledge. Dispatch selects configuration by the
// desired emission's sender identity, never by payload bits or decode outcome.
class NodeWaveformPhy final : public contracts::ITxPhy, public contracts::IRxPhy {
 public:
  static auto Create(const std::map<contracts::NodeId,WaveformPhyConfig>& configs,
      ITransmissionWaveformArtifacts& artifacts, IWaveformReceptionObserver* observer,
      const IRecordedWaveformChannel* recorded) -> Result<NodeWaveformPhy> {
    NodeWaveformPhy result;
    if(configs.empty() || !recorded || !recorded->SupportsMixture())
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Node modems require an explicit proxy provider"});
    for(const auto& [id,config]:configs) {
      auto tx=WaveformTxPhy::Create(config,artifacts);
      auto rx=WaveformRxPhy::Create(config,artifacts,observer,recorded);
      if(!tx)return std::unexpected(tx.error());
      if(!rx)return std::unexpected(rx.error());
      result.tx_.emplace(id,std::move(*tx));
      result.rx_.emplace(id,std::move(*rx));
    }
    return result;
  }
  auto Encode(const contracts::DigitalPacket& packet,
      const contracts::TxEncodeRequest& request) const -> Result<contracts::TxEmission> override {
    const auto found=tx_.find(request.sender_node_id);
    if(found==tx_.end())return std::unexpected(Error{ErrorCode::kNotFound,"Sender modem not configured"});
    return found->second.Encode(packet,request);
  }
  auto Decode(const contracts::RxDecodeRequest& request) const -> Result<contracts::RxDecodeResult> override {
    const auto sender=request.receiver_window().desired_signal().emission().sender_node_id();
    const auto found=rx_.find(sender);
    if(found==rx_.end())return std::unexpected(Error{ErrorCode::kNotFound,"Desired sender modem not configured"});
    return found->second.Decode(request);
  }
 private:
  std::map<contracts::NodeId,WaveformTxPhy> tx_;
  std::map<contracts::NodeId,WaveformRxPhy> rx_;
};
} // namespace ns3_factory::phy::internal
