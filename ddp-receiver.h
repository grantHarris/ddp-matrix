#ifndef DDP_MATRIX_DDP_RECEIVER_H_
#define DDP_MATRIX_DDP_RECEIVER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rgb_matrix {
class RGBMatrix;
class FrameCanvas;
}

namespace ddp_matrix {

struct AppOptions {
  bool realtime = true;
  int realtime_priority = 40;
};

struct DDPHeader {
  std::uint8_t flags1{};
  std::uint8_t flags2{};
  std::uint8_t data_type{};
  std::uint8_t source_id{};
  std::uint32_t data_offset{};
  std::uint16_t data_length{};
};

class DDPReceiver {
 public:
  DDPReceiver(rgb_matrix::RGBMatrix& matrix, AppOptions app_options);

  DDPReceiver(const DDPReceiver&) = delete;
  DDPReceiver& operator=(const DDPReceiver&) = delete;
  DDPReceiver(DDPReceiver&&) = delete;
  DDPReceiver& operator=(DDPReceiver&&) = delete;

  int Run();

 private:
  void HandlePacket(const std::uint8_t* packet, std::size_t packet_size);
  void RenderFrame();

  rgb_matrix::RGBMatrix& matrix_;
  rgb_matrix::FrameCanvas* offscreen_ = nullptr;
  AppOptions app_options_;
  int width_ = 0;
  int height_ = 0;
  std::size_t frame_buffer_size_ = 0;
  std::vector<std::uint8_t> frame_buffer_;
};

}  // namespace ddp_matrix

#endif  // DDP_MATRIX_DDP_RECEIVER_H_
