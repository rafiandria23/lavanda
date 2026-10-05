#include "lavanda/graph/nodes/audio_asset_source_node.h"

#include <algorithm>
#include <memory>

#include "lavanda/resources/audio_asset.h"

namespace lavanda {

void AudioAssetSourceNode::BindAsset(const AudioAsset* asset) noexcept {
  asset_ = asset;
  position_frames_ = 0;
}

void AudioAssetSourceNode::Process(NodeProcessContext& context) noexcept {
  if (asset_ == nullptr || context.output.empty() ||
      context.output.channel_count() != channel_count_) {
    context.output.Clear();
    return;
  }

  const std::uint32_t frames =
      std::min(context.frame_count, context.output.frame_count());
  const std::uint32_t total_frames = asset_->frame_count();
  const std::uint32_t start = std::min(position_frames_, total_frames);
  const std::uint32_t copy_frames = std::min(frames, total_frames - start);

  for (std::uint32_t frame = 0; frame < copy_frames; ++frame) {
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
      context.output(frame, channel) = asset_->sample(start + frame, channel);
    }
  }

  for (std::uint32_t frame = copy_frames; frame < frames; ++frame) {
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
      context.output(frame, channel) = 0.0f;
    }
  }

  position_frames_ = start + copy_frames;
}

std::unique_ptr<AudioNode> AudioAssetSourceNode::Clone() const {
  return std::make_unique<AudioAssetSourceNode>(asset_id_, channel_count_);
}

}  // namespace lavanda
