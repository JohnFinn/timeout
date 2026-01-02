#include <chrono>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

namespace {
void throw_errno() { throw std::runtime_error(strerror(errno)); }

class SubprocessHandle {
  pid_t _pid;

public:
  explicit SubprocessHandle(pid_t pid) : _pid(pid) {}

  static std::optional<SubprocessHandle> fork() {
    if (int pid = ::fork(); pid > 0) {
      return std::optional<SubprocessHandle>(pid);
    } else if (pid == 0) {
      return std::nullopt;
    } else {
      throw_errno();
    }
  };

  SubprocessHandle(const SubprocessHandle &) = delete;
  SubprocessHandle &operator=(const SubprocessHandle &) = delete;
  SubprocessHandle(SubprocessHandle &&) = delete;
  SubprocessHandle &operator=(SubprocessHandle &&) = delete;

  void kill() { ::kill(_pid, SIGKILL); }

  ~SubprocessHandle() { ::waitpid(_pid, nullptr, 0); }
};

struct pipe_fds {
private:
  int _fds[2];

  int read_fd() const { return _fds[0]; }
  int write_fd() const { return _fds[1]; }

public:
  pipe_fds(const pipe_fds &) = delete;
  pipe_fds &operator=(const pipe_fds &) = delete;
  pipe_fds(pipe_fds &&) = delete;
  pipe_fds &operator=(pipe_fds &&) = delete;

  pipe_fds() {
    if (pipe(_fds) != 0) {
      throw_errno();
    }
  }

  ~pipe_fds() noexcept(false) {
    if (close(read_fd()) != 0)
      throw_errno();
    if (close(write_fd()) != 0)
      throw_errno();
  }

  void read(std::span<std::byte> span) {
    if (::read(read_fd(), span.data(), span.size_bytes()) !=
        span.size_bytes()) {
      throw_errno();
    }
  }

  void write(std::span<const std::byte> span) {
    if (::write(write_fd(), span.data(), span.size_bytes()) !=
        span.size_bytes()) {
      throw_errno();
    }
  }

  bool poll_read(std::chrono::milliseconds timeout) {
    pollfd pfd{.fd = read_fd(), .events = POLLIN, .revents = 0};
    if (auto ret = ::poll(&pfd, 1, timeout.count()); ret > 0) {
      return true;
    } else if (ret == 0) {
      return false;
    } else {
      throw_errno();
    }
  }

  template <typename T> void serialize(const T &value) {
    using buffer_t = std::array<std::byte, sizeof(T)>;
    const auto arr = std::bit_cast<buffer_t>(value);
    write(std::span(arr));
  }

  template <typename T> T deserialize() {
    using buffer_t = std::array<std::byte, sizeof(T)>;
    buffer_t buffer;
    read(buffer);
    return std::bit_cast<T>(buffer);
  }
};

} // namespace

template <typename F>
  requires std::is_trivially_copyable_v<std::invoke_result_t<F>>
const std::optional<std::invoke_result_t<F>>
timeout(std::chrono::seconds duration, F f) {
  pipe_fds p;
  using res_t = std::invoke_result_t<F>;
  if (auto subprocess = SubprocessHandle::fork(); subprocess.has_value()) {
    if (p.poll_read(duration)) {
      return p.deserialize<res_t>();
    }
    subprocess->kill();
    return std::nullopt;
  } else {
    p.serialize(f());
    std::exit(0);
  }
};
