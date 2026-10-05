#ifndef LAVANDA_GRAPH_NODES_AUDIO_ASSET_SOURCE_NODE_H_
#define LAVANDA_GRAPH_NODES_AUDIO_ASSET_SOURCE_NODE_H_

#include <cstdint>
#include <memory>

#include "lavanda/graph/node.h"
#include "lavanda/resources/audio_asset_id.h"

namespace lavanda {

class AudioAssetSourceNode final : public AudioNode {
 public:
  AudioAssetSourceNode(AudioAssetId asset_id,
                       std::uint32_t channel_count) noexcept
      : asset_id_(asset_id), channel_count_(channel_count) {}

  void Process(NodeProcessContext& context) noexcept override;

  std::uint32_t input_channel_count() const noexcept override { return 0; }
  std::uint32_t output_channel_count() const noexcept override {
    return channel_count_;
  }
  std::uint32_t expected_input_count() const noexcept override { return 0; }

  std::unique_ptr<AudioNode> Clone() const override;

  AudioAssetId required_asset() const noexcept override { return asset_id_; }
  void BindAsset(const AudioAsset* asset) noexcept override;

 private:
  AudioAssetId asset_id_;
  std::uint32_t channel_count_;
  const AudioAsset* asset_ = nullptr;
  std::uint32_t position_frames_ = 0;
};

}  // namespace lavanda

#endif
