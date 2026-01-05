#include <chrono>
#include <iostream>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <span>
#include <stdexcept>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <tuple>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace {
void throw_errno() { throw std::runtime_error(strerror(errno)); }

class SubprocessHandle {
  std::optional<pid_t> _pid;

  explicit SubprocessHandle() : _pid(std::nullopt) {}

public:
  explicit SubprocessHandle(pid_t pid) : _pid(pid) {}

  static SubprocessHandle fork() {
    if (int pid = ::fork(); pid > 0) {
      return SubprocessHandle(pid);
    } else if (pid == 0) {
      return SubprocessHandle();
    } else {
      throw_errno();
    }
  };

  SubprocessHandle(const SubprocessHandle &) = delete;
  SubprocessHandle &operator=(const SubprocessHandle &) = delete;
  SubprocessHandle(SubprocessHandle &&other)
      : _pid(std::exchange(other._pid, std::nullopt)) {}
  SubprocessHandle &operator=(SubprocessHandle &&other) {
    _pid = std::exchange(other._pid, std::nullopt);
    return *this;
  }

  void kill() { ::kill(_pid.value(), SIGKILL); }

  operator bool() const { return _pid.has_value(); }

  ~SubprocessHandle() {
    if (_pid)
      ::waitpid(*_pid, nullptr, 0);
  }
};

struct pipe_fds {
private:
  struct fds {
    int _fds[2];
  };

  std::optional<fds> _fds;

  int read_fd() const { return _fds->_fds[0]; }
  int write_fd() const { return _fds->_fds[1]; }

public:
  pipe_fds(const pipe_fds &) = delete;
  pipe_fds &operator=(const pipe_fds &) = delete;
  pipe_fds(pipe_fds &&other) : _fds(std::exchange(other._fds, std::nullopt)) {}
  pipe_fds &operator=(pipe_fds &&other) {
    _fds = std::exchange(other._fds, std::nullopt);
    return *this;
  }

  pipe_fds() : _fds(fds()) {
    if (pipe(_fds->_fds) != 0) {
      throw_errno();
    }
  }

  ~pipe_fds() noexcept(false) {
    if (_fds) {
      if (close(read_fd()) != 0)
        throw_errno();
      if (close(write_fd()) != 0)
        throw_errno();
    }
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

  template <typename... Ts> struct trivially_copyable_tuple;

  template <> struct trivially_copyable_tuple<> {
    auto to_tuple() const { return std::tuple{}; }
  };

  template <typename T, typename... Ts>
  struct trivially_copyable_tuple<T, Ts...> {
    T head;
    trivially_copyable_tuple<Ts...> tail;

    std::tuple<T, Ts...> to_tuple() const {
      return std::tuple_cat(std::tuple{head}, tail.to_tuple());
    }
  };

  static_assert(
      std::is_trivially_copyable_v<trivially_copyable_tuple<int, int>>);

  template <typename... Ts> struct make_trivially_copyable_tuple;

  template <typename... Ts> static auto foo(const std::tuple<Ts...> &t) {
    return make_trivially_copyable_tuple<Ts...>{}(t);
  }

  template <> struct make_trivially_copyable_tuple<> {
    trivially_copyable_tuple<> operator()(const std::tuple<> &) { return {}; }
  };

  template <typename T, size_t... Is>
  static auto drop_first(const T &t, std::index_sequence<Is...>) {
    return std::tuple{std::get<Is + 1>(t)...};
  }

  template <typename T, typename... Ts>
  struct make_trivially_copyable_tuple<T, Ts...> {
    trivially_copyable_tuple<T, Ts...>
    operator()(const std::tuple<T, Ts...> &t) {
      return {std::get<0>(t),
              foo(drop_first(t, std::index_sequence_for<Ts...>()))};
    }
  };

  template <typename... Ts>
  void serialize_tuple(const std::tuple<Ts...> &value) {
    serialize(make_trivially_copyable_tuple<Ts...>{}(value));
  }

  template <typename... Ts> std::tuple<Ts...> deserialize_tuple() {
    static_assert(
        std::is_trivially_copyable_v<trivially_copyable_tuple<Ts...>>);
    return deserialize<trivially_copyable_tuple<Ts...>>().to_tuple();
  }
};

template <typename F, typename... Args> class Worker {
  pipe_fds retval_pipe;
  pipe_fds args_pipe;
  using tuple_t = std::tuple<Args...>;
  using res_t = std::invoke_result_t<F, Args...>;
  SubprocessHandle subprocess;
  F f;

public:
  Worker(F f) : f(std::move(f)), subprocess(SubprocessHandle::fork()) {
    if (!subprocess) {
      while (true) {
        retval_pipe.serialize(
            std::apply(f, args_pipe.deserialize_tuple<Args...>()));
      }
    }
  }
  Worker(Worker &&) = default;
  Worker &operator=(Worker &&) = default;

  std::optional<res_t> operator()(std::chrono::milliseconds duration,
                                  const Args &...args) {
    args_pipe.serialize_tuple(tuple_t(args...));
    if (retval_pipe.poll_read(duration)) {
      return retval_pipe.deserialize<res_t>();
    }
    subprocess.kill();
    return std::nullopt;
  }

  ~Worker() { subprocess.kill(); }
};

} // namespace

template <typename F, typename... Args>
  requires std::is_trivially_copyable_v<std::invoke_result_t<F, Args...>>
const std::optional<std::invoke_result_t<F, Args...>>
timeout(std::chrono::milliseconds duration, F f, const Args &...args) {
  static thread_local Worker<F, Args...> worker(f);
  return worker(duration, args...);
};
