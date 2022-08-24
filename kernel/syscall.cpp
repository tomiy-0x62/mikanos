#include "syscall.hpp"

#include <array>
#include <cstdint>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <cstring>

#include "asmfunc.h"
#include "msr.hpp"
#include "logger.hpp"
#include "task.hpp"
#include "terminal.hpp"
#include "font.hpp"
#include "timer.hpp"
#include "keyboard.hpp"
#include "app_event.hpp"

namespace syscall {
  struct Result {
    uint64_t value;
    int error;
  };

  struct ResultLin {
    int64_t value;
    int error;
  };

#define SYSCALL(name) \
  Result name( \
      uint64_t arg1, uint64_t arg2, uint64_t arg3, \
      uint64_t arg4, uint64_t arg5, uint64_t arg6)

#define SYSCALL_LIN(name) \
  ResultLin name( \
      uint64_t arg1, uint64_t arg2, uint64_t arg3, \
      uint64_t arg4, uint64_t arg5, uint64_t arg6)

SYSCALL(LogString) {
  if (arg1 != kError && arg1 != kWarn && arg1 != kInfo && arg1 != kDebug) {
    return { 0, EPERM };
  }
  const char* s = reinterpret_cast<const char*>(arg2);
  const auto len = strlen(s);
  if (len > 1024) {
    return { 0, E2BIG };
  }
  Log(static_cast<LogLevel>(arg1), "%s", s);
  return { len, 0 };
}

SYSCALL(PutString) {
  const auto fd = arg1;
  const char* s = reinterpret_cast<const char*>(arg2);
  const auto len = arg3;
  if (len > 1024) {
    return { 0, E2BIG };
  }

  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { 0, EBADF };
  }
  return { task.Files()[fd]->Write(s, len), 0 };
}

SYSCALL(Exit) {
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");
  return { task.OSStackPointer(), static_cast<int>(arg1) };
}

SYSCALL(OpenWindow) {
  const int w = arg1, h = arg2, x = arg3, y = arg4;
  const auto title = reinterpret_cast<const char*>(arg5);
  const auto win = std::make_shared<ToplevelWindow>(
      w, h, screen_config.pixel_format, title);

  __asm__("cli");
  const auto layer_id = layer_manager->NewLayer()
    .SetWindow(win)
    .SetDraggable(true)
    .Move({x, y})
    .ID();
  active_layer->Activate(layer_id);

  const auto task_id = task_manager->CurrentTask().ID();
  layer_task_map->insert(std::make_pair(layer_id, task_id));
  __asm__("sti");

  return { layer_id, 0 };
}

namespace {
  template <class Func, class... Args>
  Result DoWinFunc(Func f, uint64_t layer_id_flags, Args... args) {
    const uint32_t layer_flags = layer_id_flags >> 32;
    const unsigned int layer_id = layer_id_flags & 0xffffffff;

    __asm__("cli");
    auto layer = layer_manager->FindLayer(layer_id);
    __asm__("sti");
    if (layer == nullptr) {
      return { 0, EBADF };
    }

    const auto res = f(*layer->GetWindow(), args...);
    if (res.error) {
      return res;
    }

    if ((layer_flags & 1) == 0) {
      __asm__("cli");
      layer_manager->Draw(layer_id);
      __asm__("sti");
    }

    return res;
  }
}

SYSCALL(WinWriteString) {
  return DoWinFunc(
      [](Window& win,
         int x, int y, uint32_t color, const char* s) {
        WriteString(*win.Writer(), {x, y}, s, ToColor(color));
        return Result{ 0, 0 };
      }, arg1, arg2, arg3, arg4, reinterpret_cast<const char*>(arg5));
}

SYSCALL(WinFillRectangle) {
  return DoWinFunc(
      [](Window& win,
         int x, int y, int w, int h, uint32_t color) {
        FillRectangle(*win.Writer(), {x, y}, {w, h}, ToColor(color));
        return Result{ 0, 0 };
      }, arg1, arg2, arg3, arg4, arg5, arg6);
}

SYSCALL(GetCurrentTick) {
  return { timer_manager->CurrentTick(), kTimerFreq };
}

SYSCALL(WinRedraw) {
  return DoWinFunc(
      [](Window&) {
        return Result{ 0, 0 };
      }, arg1);
}

SYSCALL(WinDrawLine) {
  return DoWinFunc(
      [](Window& win,
         int x0, int y0, int x1, int y1, uint32_t color) {
        auto sign = [](int x) {
          return (x > 0) ? 1 : (x < 0) ? -1 : 0;
        };
        const int dx = x1 - x0 + sign(x1 - x0);
        const int dy = y1 - y0 + sign(y1 - y0);

        if (dx == 0 && dy == 0) {
          win.Writer()->Write({x0, y0}, ToColor(color));
          return Result{ 0, 0 };
        }

        const auto floord = static_cast<double(*)(double)>(floor);
        const auto ceild = static_cast<double(*)(double)>(ceil);

        if (abs(dx) >= abs(dy)) {
          if (dx < 0) {
            std::swap(x0, x1);
            std::swap(y0, y1);
          }
          const auto roundish = y1 >= y0 ? floord : ceild;
          const double m = static_cast<double>(dy) / dx;
          for (int x = x0; x <= x1; ++x) {
            const int y = roundish(m * (x - x0) + y0);
            win.Writer()->Write({x, y}, ToColor(color));
          }
        } else {
          if (dy < 0) {
            std::swap(x0, x1);
            std::swap(y0, y1);
          }
          const auto roundish = x1 >= x0 ? floord : ceild;
          const double m = static_cast<double>(dx) / dy;
          for (int y = y0; y <= y1; ++y) {
            const int x = roundish(m * (y - y0) + x0);
            win.Writer()->Write({x, y}, ToColor(color));
          }
        }
        return Result{ 0, 0 };
      }, arg1, arg2, arg3, arg4, arg5, arg6);
}

SYSCALL(CloseWindow) {
  const unsigned int layer_id = arg1 & 0xffffffff;
  const auto err = CloseLayer(layer_id);
  if (err.Cause() == Error::kNoSuchEntry) {
    return { EBADF, 0 };
  }
  return { 0, 0 };
}

SYSCALL(ReadEvent) {
  if (arg1 < 0x8000'0000'0000'0000) {
    return { 0, EFAULT };
  }
  const auto app_events = reinterpret_cast<AppEvent*>(arg1);
  const size_t len = arg2;

  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");
  size_t i = 0;

  while (i < len) {
    __asm__("cli");
    auto msg = task.ReceiveMessage();
    if (!msg && i == 0) {
      task.Sleep();
      continue;
    }
    __asm__("sti");

    if (!msg) {
      break;
    }

    switch (msg->type) {
    case Message::kKeyPush:
      if (msg->arg.keyboard.keycode == 20 /* Q key */ &&
          msg->arg.keyboard.modifier & (kLControlBitMask | kRControlBitMask)) {
        app_events[i].type = AppEvent::kQuit;
        ++i;
      } else {
        app_events[i].type = AppEvent::kKeyPush;
        app_events[i].arg.keypush.modifier = msg->arg.keyboard.modifier;
        app_events[i].arg.keypush.keycode = msg->arg.keyboard.keycode;
        app_events[i].arg.keypush.ascii = msg->arg.keyboard.ascii;
        app_events[i].arg.keypush.press = msg->arg.keyboard.press;
        ++i;
      }
      break;
    case Message::kMouseMove:
      app_events[i].type = AppEvent::kMouseMove;
      app_events[i].arg.mouse_move.x = msg->arg.mouse_move.x;
      app_events[i].arg.mouse_move.y = msg->arg.mouse_move.y;
      app_events[i].arg.mouse_move.dx = msg->arg.mouse_move.dx;
      app_events[i].arg.mouse_move.dy = msg->arg.mouse_move.dy;
      app_events[i].arg.mouse_move.buttons = msg->arg.mouse_move.buttons;
      ++i;
      break;
    case Message::kMouseButton:
      app_events[i].type = AppEvent::kMouseButton;
      app_events[i].arg.mouse_button.x = msg->arg.mouse_button.x;
      app_events[i].arg.mouse_button.y = msg->arg.mouse_button.y;
      app_events[i].arg.mouse_button.press = msg->arg.mouse_button.press;
      app_events[i].arg.mouse_button.button = msg->arg.mouse_button.button;
      ++i;
      break;
    case Message::kTimerTimeout:
      if (msg->arg.timer.value < 0) {
        app_events[i].type = AppEvent::kTimerTimeout;
        app_events[i].arg.timer.timeout = msg->arg.timer.timeout;
        app_events[i].arg.timer.value = -msg->arg.timer.value;
        ++i;
      }
      break;
    case Message::kWindowClose:
      app_events[i].type = AppEvent::kQuit;
      ++i;
      break;
    default:
      Log(kInfo, "uncaught event type: %u\n", msg->type);
    }
  }

  return { i, 0 };
}

SYSCALL(CreateTimer) {
  const unsigned int mode = arg1;
  const int timer_value = arg2;
  if (timer_value <= 0) {
    return { 0, EINVAL };
  }

  __asm__("cli");
  const uint64_t task_id = task_manager->CurrentTask().ID();
  __asm__("sti");

  unsigned long timeout = arg3 * kTimerFreq / 1000;
  if (mode & 1) { // relative
    timeout += timer_manager->CurrentTick();
  }

  __asm__("cli");
  timer_manager->AddTimer(Timer{timeout, -timer_value, task_id});
  __asm__("sti");
  return { timeout * 1000 / kTimerFreq, 0 };
}

namespace {
  size_t AllocateFD(Task& task) {
    const size_t num_files = task.Files().size();
    for (size_t i = 0; i < num_files; ++i) {
      if (!task.Files()[i]) {
        return i;
      }
    }
    task.Files().emplace_back();
    return num_files;
  }

  size_t AllocateFDGE(Task& task, int min) {
    const size_t num_files = task.Files().size();
    for (size_t i = 0; i < num_files; ++i) {
      if (!task.Files()[i] && i >= min) {
        return i;
      }
    }
    task.Files().emplace_back();
    return num_files;
  }

  std::pair<fat::DirectoryEntry*, int> CreateFile(const char* path) {
    auto [ file, err ] = fat::CreateFile(path);
    switch (err.Cause()) {
    case Error::kIsDirectory: return { file, EISDIR };
    case Error::kNoSuchEntry: return { file, ENOENT };
    case Error::kNoEnoughMemory: return { file, ENOSPC };
    default: return { file, 0 };
    }
  }
} // namespace

SYSCALL(OpenFile) {
  const char* path = reinterpret_cast<const char*>(arg1);
  const int flags = arg2;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (strcmp(path, "@stdin") == 0) {
    return { 0, 0 };
  }

  auto [ file, post_slash ] = fat::FindFile(path);
  if (file == nullptr) {
    if ((flags & O_CREAT) == 0) {
      return { 0, ENOENT };
    }
    auto [ new_file, err ] = CreateFile(path);
    if (err) {
      return { 0, err };
    }
    file = new_file;
  } else if (file->attr != fat::Attribute::kDirectory && post_slash) {
    return { 0, ENOENT };
  }

  size_t fd = AllocateFD(task);
  task.Files()[fd] = std::make_unique<fat::FileDescriptor>(*file);
  return { fd, 0 };
}

SYSCALL(ReadFile) {
  const int fd = arg1;
  void* buf = reinterpret_cast<void*>(arg2);
  size_t count = arg3;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { 0, EBADF };
  }
  return { task.Files()[fd]->Read(buf, count), 0 };
}

SYSCALL(DemandPages) {
  const size_t num_pages = arg1;
  // const int flags = arg2;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  const uint64_t dp_end = task.DPagingEnd();
  task.SetDPagingEnd(dp_end + 4096 * num_pages);
  return { dp_end, 0 };
}

SYSCALL(MapFile) {
  const int fd = arg1;
  size_t* file_size = reinterpret_cast<size_t*>(arg2);
  // const int flags = arg3;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { 0, EBADF };
  }

  *file_size = task.Files()[fd]->Size();
  const uint64_t vaddr_end = task.FileMapEnd();
  const uint64_t vaddr_begin = (vaddr_end - *file_size) & 0xffff'ffff'ffff'f000;
  task.SetFileMapEnd(vaddr_begin);
  task.FileMaps().push_back(FileMapping{fd, vaddr_begin, vaddr_end});
  return { vaddr_begin, 0 };
}

SYSCALL(IsTerminal) {
  const int fd = arg1;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { 0, EBADF };
  }

  return { task.Files()[fd]->IsTerminal(), 0 };
}

// Linux System call

SYSCALL_LIN(read) {
  const int fd = arg1;
  void* buf = reinterpret_cast<void*>(arg2);
  size_t count = arg3;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { -1, EBADF };
  }
  return { static_cast<int64_t>(task.Files()[fd]->Read(buf, count)), 0 };
}

SYSCALL_LIN(write) {
  const auto fd = arg1;
  const char* s = reinterpret_cast<const char*>(arg2);
  const auto len = arg3;
  if (len > 1024) {
    return { -1, E2BIG };
  }

  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { -1, EBADF };
  }
  return { static_cast<int64_t>(task.Files()[fd]->Write(s, len)), 0 };
}

SYSCALL_LIN(open) {
    const char* path = reinterpret_cast<const char*>(arg1);
  const int flags = arg2;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (strcmp(path, "stdin") == 0) {
    return { 0, 0 };
  }

  auto [ file, post_slash ] = fat::FindFile(path);
  if (file == nullptr) {
    if ((flags & O_CREAT) == 0) {
      return { 0, ENOENT };
    }
    auto [ new_file, err ] = CreateFile(path);
    if (err) {
      return { -1, err };
    }
    file = new_file;
  } else if (file->attr != fat::Attribute::kDirectory && post_slash) {
    return { -1, ENOENT };
  }

  size_t fd = AllocateFD(task);
  task.Files()[fd] = std::make_unique<fat::FileDescriptor>(*file);
  return { static_cast<int64_t>(fd), 0 };
}

SYSCALL_LIN(close) {
  const int fd = arg1;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { -1, EBADF };
  }
  task.Files().erase(task.Files().begin() + fd);
  return { 0, 0 };
}

SYSCALL_LIN(lseek) {
  // off_t lseek(int fd, off_t offset, int whence);
  const auto fd = arg1;
  off_t offset = arg2;
  int whence = arg3;

  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
    return { -1, EBADF };
  }

  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    return { -1, EINVAL };
  }

  off_t off = task.Files()[fd]->SetOffset(offset, whence);

  if (off < 0) {
    return { -1, ESPIPE };
  } else {
    return { off, 0 };
  }
}

SYSCALL_LIN(fstat) {
  return { 0, 0 };
}

SYSCALL_LIN(brk) {
  const size_t p_break = arg1;
  // const int flags = arg2;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  uint64_t dp_end;
  if (p_break == 0) {
    dp_end = task.DPagingEnd();
    
  } else {
    dp_end = p_break;
  }
  task.SetDPagingEnd(dp_end);
  return { static_cast<int64_t>(dp_end), 0 };
}

SYSCALL_LIN(rt_sigaction) {

  return { 0, 0 };
}

SYSCALL_LIN(ioctl) {

  return { 0, 0 };
}

struct iovec {
  void  *iov_base;    /* Starting address */
  size_t iov_len;     /* Number of bytes to transfer */
};

SYSCALL_LIN(writev) {
  // ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

  const auto fd = arg1;
  const struct iovec* iov = reinterpret_cast<const struct iovec *>(arg2);
  int iovcnt = arg3;
  uint64_t writedsize = 0;

  for (int i = 0; i < iovcnt; i++) {
    const auto len = iov->iov_len;
    const char* s = reinterpret_cast<const char *>(iov->iov_base);
    if (len > 1024) {
      return { -1, E2BIG };
    }

    __asm__("cli");
    auto& task = task_manager->CurrentTask();
    __asm__("sti");

    if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
      return { static_cast<int64_t>(writedsize), EBADF };
    }
    writedsize += task.Files()[fd]->Write(s, len);
    iov++;
  }
  return { static_cast<int64_t>(writedsize), 0 };

}

struct utsname {
  char sysname[65];    /* Operating system name (e.g., "Linux") */
  char nodename[65];   /* Name within "some implementation-defined
                        network" */
  char release[65];    /* Operating system release (e.g., "2.6.28") */
  char version[65];    /* Operating system version */
  char machine[65];    /* Hardware identifier */
  #ifdef _GNU_SOURCE
  char domainname[65]; /* NIS or YP domain name */
  #endif
};

SYSCALL_LIN(uname) {
  struct utsname *buf = reinterpret_cast<struct utsname*>(arg1);

  strncpy(buf->sysname, "mikanOS",  sizeof(buf->sysname));
  strncpy(buf->nodename, "unknow", sizeof(buf->nodename));
  strncpy(buf->release, "5.13.0", sizeof(buf->release));
  strncpy(buf->version, "unknow", sizeof(buf->version));
  strncpy(buf->machine, "unknow", sizeof(buf->machine));
  #ifdef _GNU_SOURCE
  strncpy(buf->domainname , "unknow", sizeof(buf->domainname));
  #endif

  return { 0, 0 };
}

SYSCALL_LIN(fcntl) {
  const auto fd = arg1;
  const auto cmd = arg2;
  
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (cmd == F_DUPFD) {
    size_t fd = AllocateFDGE(task, arg3);
    // task.Files()[fd] = std::make_unique<fat::FileDescriptor>(*file); // わからん
    return { static_cast<int64_t>(fd), 0 };
  } else if (cmd == F_GETFD) {
    // TODO
  } else if (cmd == F_SETFD) {
    // TODO
  } else if (cmd == F_GETFL) {
    return { O_RDWR, 0};
  } else if (cmd == F_SETFL) {
    // TODO
  }
  return { 0, 0 };
}

SYSCALL_LIN(readlink) {
  // ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

  // char *pathname = reinterpret_cast<char *>(arg1);
  char *buf = reinterpret_cast<char *>(arg2); 
  // int bufsiz = arg3;
  
  strcpy(buf, "/");
  
  return { 1, EACCES};
}

SYSCALL_LIN(getuid) {
  return { 0, 0 };
}

SYSCALL_LIN(geteuid) {
  return { 0, 0 };
}

SYSCALL_LIN(getegid) {
  return { 0, 0 };
}

SYSCALL_LIN(getgid) {
  return { 0, 0 };
}

SYSCALL_LIN(arch_prctl) {
  return { 0, 0 };
}

SYSCALL_LIN(exit_group) {
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");
  return { static_cast<int64_t>(task.OSStackPointer()), static_cast<int>(arg1) };
}

SYSCALL_LIN(openat) {
  const int dirfd = arg1;
  const char *path = reinterpret_cast<const char*>(arg2);
  const int flags = arg3;
  __asm__("cli");
  auto& task = task_manager->CurrentTask();
  __asm__("sti");

  if (strcmp(path, "stdin") == 0) {
    return { 0, 0 };
  }

  auto [ file, post_slash ] = fat::FindFile(path);
  if (file == nullptr) {
    if ((flags & O_CREAT) == 0) {
      return { 0, ENOENT };
    }
    auto [ new_file, err ] = CreateFile(path);
    if (err) {
      return { -1, err };
    }
    file = new_file;
  } else if (file->attr != fat::Attribute::kDirectory && post_slash) {
    return { -1, ENOENT };
  }

  size_t fd = AllocateFD(task);
  task.Files()[fd] = std::make_unique<fat::FileDescriptor>(*file);
  return { static_cast<int64_t>(fd), 0 };
}

SYSCALL_LIN(dummy) {
  unsigned int syscallNum = getEAX();
  const char *msg1 = "Dummy Syscall called\n";
  char msg2[200];
  char msg3[100];
  int length2 = std::snprintf(msg2, sizeof(msg2), "arg1:0x%016lX arg2:0x%016lX\narg3:0x%016lX arg4:0x%016lX\narg5:0x%016lX arg6:0x%016lX\n", arg1, arg2, arg3, arg4, arg5, arg6);
  int length3 = std::snprintf(msg3, sizeof(msg3), "System Call Number: 0x%08X is not implemented.\n", syscallNum);
  if (length2 > sizeof(msg2)) {
    strcpy(msg2, "dummy syscall: message too long");
  }
  if (length3 > sizeof(msg3)) {
    strcpy(msg3, "dummy syscall: message too long");
  }
  syscall::PutString(1, (uint64_t)msg1, strlen(msg1), 1, 1, 1);
  syscall::PutString(1, (uint64_t)msg2, length2, 1, 1, 1);
  syscall::PutString(1, (uint64_t)msg3, length3, 1, 1, 1);
  while (true) __asm__("hlt");
  return { 0, 0 };
}

#undef SYSCALL

} // namespace syscall

extern "C" syscall::Result invalid_Syscall_num(unsigned int syscallNum){
  const char *msg1 = "Error: Invalid Syscall Number\n";
  char s[100]; // スタック上
  // 他の方法としてnew char[100], malloc(100) ヒープ領域に生成
  // 場所は違うけど、どちらも100byteの配列が作られる
  int length = std::snprintf(s, sizeof(s), "There is no Syscall Number: 0x%08X\n", syscallNum);
  // PUtString/Writeは書き込むべきbyte数 -> null文字は含まない
  if (length > sizeof(s)) {
    strcpy(s, "invalid_Syscall_num: message too long");
  }
  syscall::PutString(1, (uint64_t)msg1, strlen(msg1), 1, 1, 1);
  syscall::PutString(1, (uint64_t)s, length, 1, 1, 1);
  return syscall::Exit(-1, 1, 1, 1, 1, 1);
}

extern "C" unsigned int LogSyscallNum() {

  unsigned int syscallNum = getEAX();

  char s[100];
  int length = std::snprintf(s, sizeof(s), "Called Syscall: 0x%08X", syscallNum);
  if (length > sizeof(s)) {
    strcpy(s, "LogSyscall: message too long");
  }
  if (length > 1024) {
    return syscallNum;
  }
  Log(kError, "%s", s);
  return syscallNum;
}

extern "C" unsigned int LogSyscallRet() {

  int64_t syscallRet = getRAX();

  char s[100];
  int length = std::snprintf(s, sizeof(s), " : ret=0x%08X\n", syscallRet);
  if (length > sizeof(s)) {
    strcpy(s, "LogSyscall: message too long");
  }
  if (length > 1024) {
    return syscallRet;
  }
  Log(kError, "%s", s);
  return syscallRet;
}

using SyscallFuncType = syscall::Result (uint64_t, uint64_t, uint64_t,
                                         uint64_t, uint64_t, uint64_t);

using SyscallLinFuncType = syscall::ResultLin (uint64_t, uint64_t, uint64_t,
                                         uint64_t, uint64_t, uint64_t);

extern "C" constexpr unsigned int numSyscall = 0x11;
extern "C" std::array<SyscallFuncType*, numSyscall> syscall_table{
  /* 0x00 */ syscall::LogString,
  /* 0x01 */ syscall::PutString,
  /* 0x02 */ syscall::Exit,
  /* 0x03 */ syscall::OpenWindow,
  /* 0x04 */ syscall::WinWriteString,
  /* 0x05 */ syscall::WinFillRectangle,
  /* 0x06 */ syscall::GetCurrentTick,
  /* 0x07 */ syscall::WinRedraw,
  /* 0x08 */ syscall::WinDrawLine,
  /* 0x09 */ syscall::CloseWindow,
  /* 0x0a */ syscall::ReadEvent,
  /* 0x0b */ syscall::CreateTimer,
  /* 0x0c */ syscall::OpenFile,
  /* 0x0d */ syscall::ReadFile,
  /* 0x0e */ syscall::DemandPages,
  /* 0x0f */ syscall::MapFile,
  /* 0x10 */ syscall::IsTerminal,
};

extern "C" constexpr unsigned int numLinSyscall = 0x14f;
extern "C" std::array<SyscallLinFuncType*, numLinSyscall> syscall_table_lin{
  /* 0x000 */ syscall::read,
  /* 0x001 */ syscall::write,
  /* 0x002 */ syscall::open,
  /* 0x003 */ syscall::close,
  /* 0x004 */ syscall::dummy, // stat
  /* 0x005 */ syscall::fstat,
  /* 0x006 */ syscall::dummy, // lstat
  /* 0x007 */ syscall::dummy, // poll
  /* 0x008 */ syscall::lseek,
  /* 0x009 */ syscall::dummy, // mmap
  /* 0x00a */ syscall::dummy, // mprotect
  /* 0x00b */ syscall::dummy, // munmap
  /* 0x00c */ syscall::brk,
  /* 0x00d */ syscall::rt_sigaction,
  /* 0x00e */ syscall::dummy, // rt_sigprocmask
  /* 0x00f */ syscall::dummy, // rt_sigreturn
  /* 0x010 */ syscall::ioctl,
  /* 0x011 */ syscall::dummy, // pread
  /* 0x012 */ syscall::dummy, // pwrite
  /* 0x013 */ syscall::dummy, // readv
  /* 0x014 */ syscall::writev,
  /* 0x015 */ syscall::dummy, // access
  /* 0x016 */ syscall::dummy, // pipe
  /* 0x017 */ syscall::dummy, // select
  /* 0x018 */ syscall::dummy, // sched_yield
  /* 0x019 */ syscall::dummy, // mremap
  /* 0x01a */ syscall::dummy, // msync
  /* 0x01b */ syscall::dummy, // mincore
  /* 0x01c */ syscall::dummy, // madvise
  /* 0x01d */ syscall::dummy, // shmget
  /* 0x01e */ syscall::dummy, // shmat
  /* 0x01f */ syscall::dummy, // shmctl
  /* 0x020 */ syscall::dummy, // dup
  /* 0x021 */ syscall::dummy, // dup2
  /* 0x022 */ syscall::dummy, // pause
  /* 0x023 */ syscall::dummy, // nanosleep
  /* 0x024 */ syscall::dummy, // getitimer
  /* 0x025 */ syscall::dummy, // alarm
  /* 0x026 */ syscall::dummy, // setitimer
  /* 0x027 */ syscall::dummy, // getpid
  /* 0x028 */ syscall::dummy, // sendfile
  /* 0x029 */ syscall::dummy, // socket
  /* 0x02a */ syscall::dummy, // connect
  /* 0x02b */ syscall::dummy, // accept
  /* 0x02c */ syscall::dummy, // sendto
  /* 0x02d */ syscall::dummy, // recvfrom
  /* 0x02e */ syscall::dummy, // sendmsg
  /* 0x02f */ syscall::dummy, // recvmsg
  /* 0x030 */ syscall::dummy, // shutdown
  /* 0x031 */ syscall::dummy, // bind
  /* 0x032 */ syscall::dummy, // listen
  /* 0x033 */ syscall::dummy, // getsockname
  /* 0x034 */ syscall::dummy, // getpeername
  /* 0x035 */ syscall::dummy, // socketpair
  /* 0x036 */ syscall::dummy, // setsockopt
  /* 0x037 */ syscall::dummy, // getsockopt
  /* 0x038 */ syscall::dummy, // clone
  /* 0x039 */ syscall::dummy, // fork
  /* 0x03a */ syscall::dummy, // vfork
  /* 0x03b */ syscall::dummy, // execve
  /* 0x03c */ syscall::dummy, // exit
  /* 0x03d */ syscall::dummy, // wait4
  /* 0x03e */ syscall::dummy, // kill
  /* 0x03f */ syscall::uname,
  /* 0x040 */ syscall::dummy, // semget
  /* 0x041 */ syscall::dummy, // semop
  /* 0x042 */ syscall::dummy, // semctl
  /* 0x043 */ syscall::dummy, // shmdt
  /* 0x044 */ syscall::dummy, // msgget
  /* 0x045 */ syscall::dummy, // msgsnd
  /* 0x046 */ syscall::dummy, // msgrcv
  /* 0x047 */ syscall::dummy, // msgctl
  /* 0x048 */ syscall::fcntl,
  /* 0x049 */ syscall::dummy, // flock
  /* 0x04a */ syscall::dummy, // fsync
  /* 0x04b */ syscall::dummy, // fdatasync
  /* 0x04c */ syscall::dummy, // truncate
  /* 0x04d */ syscall::dummy, // ftruncate
  /* 0x04e */ syscall::dummy, // getdents
  /* 0x04f */ syscall::dummy, // getcwd
  /* 0x050 */ syscall::dummy, // chdir
  /* 0x051 */ syscall::dummy, // fchdir
  /* 0x052 */ syscall::dummy, // rename
  /* 0x053 */ syscall::dummy, // mkdir
  /* 0x054 */ syscall::dummy, // rmdir
  /* 0x055 */ syscall::dummy, // creat
  /* 0x056 */ syscall::dummy, // link
  /* 0x057 */ syscall::dummy, // unlink
  /* 0x058 */ syscall::dummy, // symlink
  /* 0x059 */ syscall::readlink,
  /* 0x05a */ syscall::dummy, // chmod
  /* 0x05b */ syscall::dummy, // fchmod
  /* 0x05c */ syscall::dummy, // chown
  /* 0x05d */ syscall::dummy, // fchown
  /* 0x05e */ syscall::dummy, // lchown
  /* 0x05f */ syscall::dummy, // umask
  /* 0x060 */ syscall::dummy, // gettimeofday
  /* 0x061 */ syscall::dummy, // getrlimit
  /* 0x062 */ syscall::dummy, // getrusage
  /* 0x063 */ syscall::dummy, // sysinfo
  /* 0x064 */ syscall::dummy, // times
  /* 0x065 */ syscall::dummy, // ptrace
  /* 0x066 */ syscall::getuid,
  /* 0x067 */ syscall::dummy, // syslog
  /* 0x068 */ syscall::getgid,
  /* 0x069 */ syscall::dummy, //  setuid
  /* 0x06a */ syscall::dummy, //  setgid
  /* 0x06b */ syscall::geteuid,
  /* 0x06c */ syscall::getegid,
  /* 0x06d */ syscall::dummy, // setpgid
  /* 0x06e */ syscall::dummy, // getppid
  /* 0x06f */ syscall::dummy, // getpgrp
  /* 0x070 */ syscall::dummy, // setsid
  /* 0x071 */ syscall::dummy, // setreuid
  /* 0x072 */ syscall::dummy, // setregid
  /* 0x073 */ syscall::dummy, // getgroups
  /* 0x074 */ syscall::dummy, // setgroups
  /* 0x075 */ syscall::dummy, // setresuid
  /* 0x076 */ syscall::dummy, // getresuid
  /* 0x077 */ syscall::dummy, // setresgid
  /* 0x078 */ syscall::dummy, // getresgid
  /* 0x079 */ syscall::dummy, // getpgid
  /* 0x07a */ syscall::dummy, // setfsuid
  /* 0x07b */ syscall::dummy, // setfsgid
  /* 0x07c */ syscall::dummy, // getsid
  /* 0x07d */ syscall::dummy, // capget
  /* 0x07e */ syscall::dummy, // capset
  /* 0x07f */ syscall::dummy, // rt_sigpending
  /* 0x080 */ syscall::dummy, // rt_sigtimedwait
  /* 0x081 */ syscall::dummy, // rt_sigqueueinfo
  /* 0x082 */ syscall::dummy, // rt_sigsuspend
  /* 0x083 */ syscall::dummy, // sigaltstack
  /* 0x084 */ syscall::dummy, // utime
  /* 0x085 */ syscall::dummy, // mknod
  /* 0x086 */ syscall::dummy, // uselib
  /* 0x087 */ syscall::dummy, // personality
  /* 0x088 */ syscall::dummy, // ustat
  /* 0x089 */ syscall::dummy, // statfs
  /* 0x08a */ syscall::dummy, // fstatfs
  /* 0x08b */ syscall::dummy, // sysfs
  /* 0x08c */ syscall::dummy, // getpriority
  /* 0x08d */ syscall::dummy, // setpriority
  /* 0x08e */ syscall::dummy, // sched_setparam
  /* 0x08f */ syscall::dummy, // sched_getparam
  /* 0x090 */ syscall::dummy, // sched_setscheduler
  /* 0x091 */ syscall::dummy, // sched_getscheduler
  /* 0x092 */ syscall::dummy, // sched_get_priority_max
  /* 0x093 */ syscall::dummy, // sched_get_priority_min
  /* 0x094 */ syscall::dummy, // sched_rr_get_interval
  /* 0x095 */ syscall::dummy, // mlock
  /* 0x096 */ syscall::dummy, // munlock
  /* 0x097 */ syscall::dummy, // mlockall
  /* 0x098 */ syscall::dummy, // munlockall
  /* 0x099 */ syscall::dummy, // vhangup
  /* 0x09a */ syscall::dummy, // modify_ldt
  /* 0x09b */ syscall::dummy, // pivot_root
  /* 0x09c */ syscall::dummy, // _sysctl
  /* 0x09d */ syscall::dummy, // prctl
  /* 0x09e */ syscall::arch_prctl,
  /* 0x09f */ syscall::dummy, // adjtimex
  /* 0x0a0 */ syscall::dummy, // setrlimit
  /* 0x0a1 */ syscall::dummy, // chroot
  /* 0x0a2 */ syscall::dummy, // sync
  /* 0x0a3 */ syscall::dummy, // acct
  /* 0x0a4 */ syscall::dummy, // settimeofday
  /* 0x0a5 */ syscall::dummy, // mount
  /* 0x0a6 */ syscall::dummy, // umount2
  /* 0x0a7 */ syscall::dummy, // swapon
  /* 0x0a8 */ syscall::dummy, // swapoff
  /* 0x0a9 */ syscall::dummy, // reboot
  /* 0x0aa */ syscall::dummy, // sethostname
  /* 0x0ab */ syscall::dummy, // setdomainname
  /* 0x0ac */ syscall::dummy, // iopl
  /* 0x0ad */ syscall::dummy, // ioperm
  /* 0x0ae */ syscall::dummy, // create_module
  /* 0x0af */ syscall::dummy, // init_module
  /* 0x0b0 */ syscall::dummy, // delete_module
  /* 0x0b1 */ syscall::dummy, // get_kernel_syms
  /* 0x0b2 */ syscall::dummy, // query_module
  /* 0x0b3 */ syscall::dummy, // quotactl
  /* 0x0b4 */ syscall::dummy, // nfsservctl
  /* 0x0b5 */ syscall::dummy, // getpmsg
  /* 0x0b6 */ syscall::dummy, // putpmsg
  /* 0x0b7 */ syscall::dummy, // afs_syscall
  /* 0x0b8 */ syscall::dummy, // tuxcall
  /* 0x0b9 */ syscall::dummy, // security
  /* 0x0ba */ syscall::dummy, // gettid
  /* 0x0bb */ syscall::dummy, // readahead
  /* 0x0bc */ syscall::dummy, // setxattr
  /* 0x0bd */ syscall::dummy, // lsetxattr
  /* 0x0be */ syscall::dummy, // fsetxattr
  /* 0x0bf */ syscall::dummy, // getxattr
  /* 0x0c0 */ syscall::dummy, // lgetxattr
  /* 0x0c1 */ syscall::dummy, // fgetxattr
  /* 0x0c2 */ syscall::dummy, // listxattr
  /* 0x0c3 */ syscall::dummy, // llistxattr
  /* 0x0c4 */ syscall::dummy, // flistxattr
  /* 0x0c5 */ syscall::dummy, // removexattr
  /* 0x0c6 */ syscall::dummy, // lremovexattr
  /* 0x0c7 */ syscall::dummy, // fremovexattr
  /* 0x0c8 */ syscall::dummy, // tkill
  /* 0x0c9 */ syscall::dummy, // time
  /* 0x0ca */ syscall::dummy, // futex
  /* 0x0cb */ syscall::dummy, // sched_setaffinity
  /* 0x0cc */ syscall::dummy, // sched_getaffinity
  /* 0x0cd */ syscall::dummy, // set_thread_area
  /* 0x0ce */ syscall::dummy, // io_setup
  /* 0x0cf */ syscall::dummy, // io_destroy
  /* 0x0d0 */ syscall::dummy, // io_getevents
  /* 0x0d1 */ syscall::dummy, // io_submit
  /* 0x0d2 */ syscall::dummy, // io_cancel
  /* 0x0d3 */ syscall::dummy, // get_thread_area
  /* 0x0d4 */ syscall::dummy, // lookup_dcookie
  /* 0x0d5 */ syscall::dummy, // epoll_create
  /* 0x0d6 */ syscall::dummy, // epoll_ctl_old
  /* 0x0d7 */ syscall::dummy, // epoll_wait_old
  /* 0x0d8 */ syscall::dummy, // remap_file_pages
  /* 0x0d9 */ syscall::dummy, // getdents64
  /* 0x0da */ syscall::dummy, // set_tid_address
  /* 0x0db */ syscall::dummy, // restart_syscall
  /* 0x0dc */ syscall::dummy, // semtimedop
  /* 0x0dd */ syscall::dummy, // fadvise64
  /* 0x0de */ syscall::dummy, // timer_create
  /* 0x0df */ syscall::dummy, // timer_settime
  /* 0x0e0 */ syscall::dummy, // timer_gettime
  /* 0x0e1 */ syscall::dummy, // timer_getoverrun
  /* 0x0e2 */ syscall::dummy, // timer_delete
  /* 0x0e3 */ syscall::dummy, // clock_settime
  /* 0x0e4 */ syscall::dummy, // clock_gettime
  /* 0x0e5 */ syscall::dummy, // clock_getres
  /* 0x0e6 */ syscall::dummy, // clock_nanosleep
  /* 0x0e7 */ syscall::exit_group,
  /* 0x0e8 */ syscall::dummy, // epoll_wait,
  /* 0x0e9 */ syscall::dummy, // epoll_ctl,
  /* 0x0ea */ syscall::dummy, // tgkill,
  /* 0x0eb */ syscall::dummy, // utimes,
  /* 0x0ec */ syscall::dummy, // vserver,
  /* 0x0ed */ syscall::dummy, // mbind,
  /* 0x0ee */ syscall::dummy, // set_mempolicy,
  /* 0x0ef */ syscall::dummy, // get_mempolicy,
  /* 0x0f0 */ syscall::dummy, // mq_open,
  /* 0x0f1 */ syscall::dummy, // mq_unlink,
  /* 0x0f2 */ syscall::dummy, // mq_timedsend,
  /* 0x0f3 */ syscall::dummy, // mq_timedreceive,
  /* 0x0f4 */ syscall::dummy, // mq_notify,
  /* 0x0f5 */ syscall::dummy, // mq_getsetattr,
  /* 0x0f6 */ syscall::dummy, // kexec_load,
  /* 0x0f7 */ syscall::dummy, // waitid,
  /* 0x0f8 */ syscall::dummy, // add_key,
  /* 0x0f9 */ syscall::dummy, // request_key,
  /* 0x0fa */ syscall::dummy, // keyctl,
  /* 0x0fb */ syscall::dummy, // ioprio_set,
  /* 0x0fc */ syscall::dummy, // ioprio_get,
  /* 0x0fd */ syscall::dummy, // inotify_init,
  /* 0x0fe */ syscall::dummy, // inotify_add_watch,
  /* 0x0ff */ syscall::dummy, // inotify_rm_watch,
  /* 0x100 */ syscall::dummy, // migrate_pages,
  /* 0x101 */ syscall::openat,
  /* 0x102 */ syscall::dummy, // mkdirat,
  /* 0x103 */ syscall::dummy, // mknodat,
  /* 0x104 */ syscall::dummy, // fchownat,
  /* 0x105 */ syscall::dummy, // futimesat,
  /* 0x106 */ syscall::dummy, // newfstatat,
  /* 0x107 */ syscall::dummy, // unlinkat,
  /* 0x108 */ syscall::dummy, // renameat,
  /* 0x109 */ syscall::dummy, // linkat,
  /* 0x10a */ syscall::dummy, // symlinkat,
  /* 0x10b */ syscall::dummy, // readlinkat,
  /* 0x10c */ syscall::dummy, // fchmodat,
  /* 0x10d */ syscall::dummy, // faccessat,
  /* 0x10e */ syscall::dummy, // pselect6,
  /* 0x10f */ syscall::dummy, // ppoll,
  /* 0x110 */ syscall::dummy, // unshare,
  /* 0x111 */ syscall::dummy, // set_robust_list,
  /* 0x112 */ syscall::dummy, // get_robust_list,
  /* 0x113 */ syscall::dummy, // splice,
  /* 0x114 */ syscall::dummy, // tee,
  /* 0x115 */ syscall::dummy, // sync_file_range,
  /* 0x116 */ syscall::dummy, // vmsplice,
  /* 0x117 */ syscall::dummy, // move_pages,
  /* 0x118 */ syscall::dummy, // utimensat,
  /* 0x119 */ syscall::dummy, // epoll_pwait,
  /* 0x11a */ syscall::dummy, // signalfd,
  /* 0x11b */ syscall::dummy, // timerfd,
  /* 0x11c */ syscall::dummy, // eventfd,
  /* 0x11d */ syscall::dummy, // fallocate,
  /* 0x11e */ syscall::dummy, // timerfd_settime,
  /* 0x11f */ syscall::dummy, // timerfd_gettime,
  /* 0x120 */ syscall::dummy, // accept4,
  /* 0x121 */ syscall::dummy, // signalfd4,
  /* 0x122 */ syscall::dummy, // eventfd2,
  /* 0x123 */ syscall::dummy, // epoll_create1,
  /* 0x124 */ syscall::dummy, // dup3,
  /* 0x125 */ syscall::dummy, // pipe2,
  /* 0x126 */ syscall::dummy, // inotify_init1,
  /* 0x127 */ syscall::dummy, // preadv,
  /* 0x128 */ syscall::dummy, // pwritev,
  /* 0x129 */ syscall::dummy, // rt_tgsigqueueinfo,
  /* 0x12a */ syscall::dummy, // perf_event_open,
  /* 0x12b */ syscall::dummy, // recvmmsg,
  /* 0x12c */ syscall::dummy, // fanotify_init,
  /* 0x12d */ syscall::dummy, // fanotify_mark,
  /* 0x12e */ syscall::dummy, // prlimit64,
  /* 0x12f */ syscall::dummy, // name_to_handle_at,
  /* 0x130 */ syscall::dummy, // open_by_handle_at,
  /* 0x131 */ syscall::dummy, // clock_adjtime,
  /* 0x132 */ syscall::dummy, // syncfs,
  /* 0x133 */ syscall::dummy, // sendmmsg,
  /* 0x134 */ syscall::dummy, // setns,
  /* 0x135 */ syscall::dummy, // getcpu,
  /* 0x136 */ syscall::dummy, // process_vm_readv,
  /* 0x137 */ syscall::dummy, // process_vm_writev,
  /* 0x138 */ syscall::dummy, // kcmp,
  /* 0x139 */ syscall::dummy, // finit_module,
  /* 0x13a */ syscall::dummy, // sched_setattr,
  /* 0x13b */ syscall::dummy, // sched_getattr,
  /* 0x13c */ syscall::dummy, // renameat2,
  /* 0x13d */ syscall::dummy, // seccomp,
  /* 0x13e */ syscall::dummy, // getrandom,
  /* 0x13f */ syscall::dummy, // memfd_create,
  /* 0x140 */ syscall::dummy, // kexec_file_load,
  /* 0x141 */ syscall::dummy, // bpf,
  /* 0x142 */ syscall::dummy, // execveat,
  /* 0x143 */ syscall::dummy, // userfaultfd,
  /* 0x144 */ syscall::dummy, // membarrier,
  /* 0x145 */ syscall::dummy, // mlock2,
  /* 0x146 */ syscall::dummy, // copy_file_range,
  /* 0x147 */ syscall::dummy, // preadv2,
  /* 0x148 */ syscall::dummy, // pwritev2,
  /* 0x149 */ syscall::dummy, // pkey_mprotect,
  /* 0x14a */ syscall::dummy, // pkey_alloc,
  /* 0x14b */ syscall::dummy, // pkey_free,
  /* 0x14c */ syscall::dummy, // statx,
  /* 0x14d */ syscall::dummy, // io_pgetevents,
  /* 0x14e */ syscall::dummy, // rseq,
};


void InitializeSyscall() {
  WriteMSR(kIA32_EFER, 0x0501u);
  WriteMSR(kIA32_LSTAR, reinterpret_cast<uint64_t>(SyscallEntry));
  WriteMSR(kIA32_STAR, static_cast<uint64_t>(8) << 32 |
                       static_cast<uint64_t>(16 | 3) << 48);
  WriteMSR(kIA32_FMASK, 0);
}
