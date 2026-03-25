// DDP (Distributed Display Protocol) receiver for rpi-rgb-led-matrix.
// Listens on UDP port 4048 for RGB pixel data and renders completed frames
// to HUB75 panels using double-buffered updates.

#include "ddp-receiver.h"

#include "ddp-receiver-config.h"
#include "led-matrix.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

namespace {

constexpr int kDdpPort = 4048;
constexpr std::size_t kDdpHeaderSize = 10;
constexpr std::size_t kDdpMaxPacketSize = 65507;
constexpr std::size_t kDdpTimecodeSize = 4;
constexpr std::size_t kBytesPerPixel = 3;
constexpr int kSocketReceiveBufferBytes = 1024 * 1024;
constexpr int kRealtimePriorityMin = 1;
constexpr int kRealtimePriorityMax = 99;

constexpr std::uint8_t kDdpPush = 0x01;
constexpr std::uint8_t kDdpQuery = 0x04;
constexpr std::uint8_t kDdpReply = 0x08;
constexpr std::uint8_t kDdpTimecode = 0x20;

volatile std::sig_atomic_t g_running = 1;

class FileDescriptor {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) noexcept : fd_(fd) {}

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  FileDescriptor(FileDescriptor&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}

  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      Reset(std::exchange(other.fd_, -1));
    }
    return *this;
  }

  ~FileDescriptor() { Reset(); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

  void Reset(int fd = -1) noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

class MatrixClearGuard {
 public:
  explicit MatrixClearGuard(rgb_matrix::RGBMatrix& matrix) noexcept
      : matrix_(matrix) {}

  MatrixClearGuard(const MatrixClearGuard&) = delete;
  MatrixClearGuard& operator=(const MatrixClearGuard&) = delete;

  ~MatrixClearGuard();

 private:
  rgb_matrix::RGBMatrix& matrix_;
};

enum class PriorityFlagParseStatus {
  kNoMatch,
  kParsed,
  kInvalid,
};

void InterruptHandler(int) noexcept {
  g_running = 0;
}

void SetupSignals() {
  g_running = 1;

  struct sigaction action {};
  action.sa_handler = InterruptHandler;
  ::sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  if (::sigaction(SIGTERM, &action, nullptr) < 0) {
    std::perror("sigaction(SIGTERM)");
  }
  if (::sigaction(SIGINT, &action, nullptr) < 0) {
    std::perror("sigaction(SIGINT)");
  }
}

[[nodiscard]] bool IsRunning() noexcept {
  return g_running != 0;
}

[[nodiscard]] rgb_matrix::RGBMatrix::Options CreateDefaultMatrixOptions() {
  rgb_matrix::RGBMatrix::Options options;
  options.hardware_mapping = ddp_matrix::build_config::kGpioMapping;
  options.rows = ddp_matrix::build_config::kPanelRows;
  options.cols = ddp_matrix::build_config::kPanelCols;
  options.chain_length = ddp_matrix::build_config::kPanelChain;
  options.parallel = ddp_matrix::build_config::kPanelParallel;
  options.brightness = ddp_matrix::build_config::kBrightness;
  return options;
}

[[nodiscard]] rgb_matrix::RuntimeOptions CreateDefaultRuntimeOptions() {
  rgb_matrix::RuntimeOptions runtime_options;
  runtime_options.gpio_slowdown = ddp_matrix::build_config::kGpioSlowdown;
  runtime_options.drop_privileges = -1;
  return runtime_options;
}

[[nodiscard]] PriorityFlagParseStatus ParsePriorityFlag(
    std::string_view argument,
    std::string_view prefix,
    int& priority_out) {
  if (argument.size() < prefix.size() ||
      argument.compare(0, prefix.size(), prefix) != 0) {
    return PriorityFlagParseStatus::kNoMatch;
  }

  char* end = nullptr;
  errno = 0;
  const long parsed =
      std::strtol(argument.data() + prefix.size(), &end, 10);

  if (errno != 0 || end != argument.data() + argument.size() ||
      parsed < kRealtimePriorityMin || parsed > kRealtimePriorityMax) {
    std::fprintf(stderr,
                 "Invalid realtime priority '%.*s'. Expected %d-%d.\n",
                 static_cast<int>(argument.size() - prefix.size()),
                 argument.data() + prefix.size(),
                 kRealtimePriorityMin,
                 kRealtimePriorityMax);
    return PriorityFlagParseStatus::kInvalid;
  }

  priority_out = static_cast<int>(parsed);
  return PriorityFlagParseStatus::kParsed;
}

[[nodiscard]] bool ParseAppFlags(int& argc,
                                 char* argv[],
                                 ddp_matrix::AppOptions& app_options) {
  int dst = 1;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);

    if (argument == "--realtime") {
      app_options.realtime = true;
      continue;
    }
    if (argument == "--no-realtime") {
      app_options.realtime = false;
      continue;
    }

    if (const auto status = ParsePriorityFlag(
            argument, "--realtime-priority=", app_options.realtime_priority);
        status == PriorityFlagParseStatus::kParsed) {
      continue;
    } else if (status == PriorityFlagParseStatus::kInvalid) {
      return false;
    }

    if (const auto status =
            ParsePriorityFlag(argument, "--rt-priority=",
                              app_options.realtime_priority);
        status == PriorityFlagParseStatus::kParsed) {
      continue;
    } else if (status == PriorityFlagParseStatus::kInvalid) {
      return false;
    }

    argv[dst++] = argv[index];
  }

  argv[dst] = nullptr;
  argc = dst;
  return true;
}

bool EnableRealtimeMode(int priority) {
#if defined(__linux__)
  if (::mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
    std::fprintf(stderr, "Warning: mlockall() failed: %s\n",
                 std::strerror(errno));
  }

  struct sched_param sched {};
  sched.sched_priority = priority;
  if (::sched_setscheduler(0, SCHED_FIFO, &sched) < 0) {
    std::fprintf(stderr,
                 "Warning: failed to enable realtime scheduling "
                 "(SCHED_FIFO, priority %d): %s\n",
                 priority,
                 std::strerror(errno));
    return false;
  }

  std::fprintf(stderr,
               "Realtime scheduling enabled (SCHED_FIFO, priority %d)\n",
               priority);
  return true;
#else
  (void)priority;
  std::fprintf(
      stderr,
      "Warning: realtime scheduling is only available on Linux builds\n");
  return false;
#endif
}

[[nodiscard]] bool ParseDDPHeader(const std::uint8_t* buffer,
                                  std::size_t length,
                                  ddp_matrix::DDPHeader& header) {
  if (length < kDdpHeaderSize) {
    return false;
  }

  header.flags1 = buffer[0];
  header.flags2 = buffer[1];
  header.data_type = buffer[2];
  header.source_id = buffer[3];
  header.data_offset = (static_cast<std::uint32_t>(buffer[4]) << 24) |
                       (static_cast<std::uint32_t>(buffer[5]) << 16) |
                       (static_cast<std::uint32_t>(buffer[6]) << 8) |
                       static_cast<std::uint32_t>(buffer[7]);
  header.data_length =
      (static_cast<std::uint16_t>(buffer[8]) << 8) |
      static_cast<std::uint16_t>(buffer[9]);
  return true;
}

std::size_t GetPayloadOffset(const ddp_matrix::DDPHeader& header) noexcept {
  return kDdpHeaderSize +
         (((header.flags1 & kDdpTimecode) != 0) ? kDdpTimecodeSize : 0U);
}

MatrixClearGuard::~MatrixClearGuard() {
  matrix_.Clear();
}

}  // namespace

namespace ddp_matrix {

DDPReceiver::DDPReceiver(rgb_matrix::RGBMatrix& matrix, AppOptions app_options)
    : matrix_(matrix),
      offscreen_(matrix.CreateFrameCanvas()),
      app_options_(app_options),
      width_(matrix.width()),
      height_(matrix.height()),
      frame_buffer_size_(static_cast<std::size_t>(width_) *
                         static_cast<std::size_t>(height_) * kBytesPerPixel),
      frame_buffer_(frame_buffer_size_, 0) {}

int DDPReceiver::Run() {
  if (offscreen_ == nullptr) {
    std::fprintf(stderr, "Failed to create offscreen frame canvas.\n");
    return EXIT_FAILURE;
  }

  MatrixClearGuard clear_guard(matrix_);

  const auto total_pixels =
      static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
  std::fprintf(stderr, "Matrix: %dx%d (%zu pixels, %zu bytes RGB)\n",
               width_, height_, total_pixels, frame_buffer_size_);

  if (app_options_.realtime) {
    EnableRealtimeMode(app_options_.realtime_priority);
  } else {
    std::fprintf(stderr, "Realtime scheduling disabled\n");
  }

  FileDescriptor socket_fd(::socket(AF_INET, SOCK_DGRAM, 0));
  if (!socket_fd) {
    std::perror("socket");
    return EXIT_FAILURE;
  }

  const int reuse = 1;
  if (::setsockopt(socket_fd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(reuse)) < 0) {
    std::perror("setsockopt(SO_REUSEADDR)");
  }

  const int receive_buffer_size = kSocketReceiveBufferBytes;
  if (::setsockopt(socket_fd.get(), SOL_SOCKET, SO_RCVBUF,
                   &receive_buffer_size, sizeof(receive_buffer_size)) < 0) {
    std::perror("setsockopt(SO_RCVBUF)");
  }

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(kDdpPort);

  if (::bind(socket_fd.get(),
             reinterpret_cast<struct sockaddr*>(&address),
             sizeof(address)) < 0) {
    std::perror("bind");
    return EXIT_FAILURE;
  }

  std::fprintf(stderr, "Listening for DDP on UDP port %d (Ctrl-C to quit)\n",
               kDdpPort);

  std::array<std::uint8_t, kDdpMaxPacketSize> packet_buffer {};
  while (IsRunning()) {
    const ssize_t bytes_received =
        ::recvfrom(socket_fd.get(), packet_buffer.data(), packet_buffer.size(),
                   0, nullptr, nullptr);
    if (bytes_received < 0) {
      if (IsRunning()) {
        std::perror("recvfrom");
      }
      break;
    }

    HandlePacket(packet_buffer.data(),
                 static_cast<std::size_t>(bytes_received));
  }

  std::fprintf(stderr, "\nDone.\n");
  return EXIT_SUCCESS;
}

void DDPReceiver::HandlePacket(const std::uint8_t* packet,
                               std::size_t packet_size) {
  DDPHeader header {};
  if (!ParseDDPHeader(packet, packet_size, header)) {
    return;
  }

  if ((header.flags1 & (kDdpQuery | kDdpReply)) != 0) {
    return;
  }

  const std::size_t payload_offset = GetPayloadOffset(header);
  if (packet_size < payload_offset) {
    return;
  }

  std::size_t copy_length = std::min<std::size_t>(
      header.data_length, packet_size - payload_offset);

  const std::size_t destination_offset = header.data_offset;
  if (destination_offset >= frame_buffer_size_) {
    return;
  }

  copy_length =
      std::min(copy_length, frame_buffer_size_ - destination_offset);
  std::memcpy(frame_buffer_.data() + destination_offset,
              packet + payload_offset, copy_length);

  if ((header.flags1 & kDdpPush) != 0) {
    RenderFrame();
  }
}

void DDPReceiver::RenderFrame() {
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      const std::size_t pixel_index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(x)) *
          kBytesPerPixel;
      offscreen_->SetPixel(x, y, frame_buffer_[pixel_index],
                           frame_buffer_[pixel_index + 1],
                           frame_buffer_[pixel_index + 2]);
    }
  }

  offscreen_ = matrix_.SwapOnVSync(offscreen_);
}

}  // namespace ddp_matrix

int main(int argc, char* argv[]) {
  ddp_matrix::AppOptions app_options{
      ddp_matrix::build_config::kEnableRealtime,
      ddp_matrix::build_config::kRealtimePriority,
  };
  if (!ParseAppFlags(argc, argv, app_options)) {
    return EXIT_FAILURE;
  }

  auto matrix_options = CreateDefaultMatrixOptions();
  auto runtime_options = CreateDefaultRuntimeOptions();
  std::unique_ptr<rgb_matrix::RGBMatrix> matrix(
      rgb_matrix::RGBMatrix::CreateFromFlags(&argc, &argv, &matrix_options,
                                             &runtime_options));
  if (matrix == nullptr) {
    std::fprintf(stderr,
                 "Failed to create matrix. Are you running as root (sudo)?\n");
    return EXIT_FAILURE;
  }

  SetupSignals();

  ddp_matrix::DDPReceiver receiver(*matrix, app_options);
  return receiver.Run();
}
