# cpoll-ng
A lightweight and fast asynchronous I/O library.

This is a rewrite of cpoll with a focus on usability and simplicity.

# Classes
## CP::Callback
This is the callback type used by all cpoll asynchronous functions. The parameter `r` refers to the return value of the equivalent synchronous version of each function.
```c++
typedef function<void(int r)> Callback;
```

## CP::File
This is the central class of the cpoll library. `File` wraps a file descriptor and allows asynchronous reads and writes.

```c++
#include <cpoll-ng/cpoll.H>

namespace CP {
class File: public FD
{
public:
	File();

	// takes ownership of fd, and sets fd to nonblocking mode.
	File(int fd);

	// same as File(int fd), but must only be called if this File was
	// constructed by the default constructor
	void init(int fd);

	// opens a file using open(2) and sets it to nonblocking mode.
	File(const char* name, int flags, int perms = 0);

	// closes the file descriptor
	~File();

	// the following functions are all nonblocking and will return an error
	// if the operation would block.
	int32_t read(void* buf, int32_t len);
	int32_t readv(iovec* iov, int iovcnt);
	int32_t write(const void* buf, int32_t len);
	int32_t writev(iovec* iov, int iovcnt);
	int32_t send(const void* buf, int32_t len, int32_t flags = 0);
	int32_t recv(void* buf, int32_t len, int32_t flags = 0);


	// async functions

	void read(void* buf, int32_t len, const Callback& cb, bool repeat = false);
	void readv(iovec* iov, int iovcnt, const Callback& cb, bool repeat = false);
	void readAll(void* buf, int32_t len, const Callback& cb);

	void write(const void* buf, int32_t len, const Callback& cb, bool repeat = false);
	void writev(iovec* iov, int iovcnt, const Callback& cb, bool repeat = false);
	void writeAll(const void* buf, int32_t len, const Callback& cb);

	void recv(void* buf, int32_t len, int32_t flags, const Callback& cb, bool repeat = false);
	void repeatRecv(void* buf, int32_t len, int32_t flags, const Callback& cb);
	void recvAll(void* buf, int32_t len, int32_t flags, const Callback& cb);

	void send(const void* buf, int32_t len, int32_t flags, const Callback& cb, bool repeat = false);
	inline void repeatSend(const void* buf, int32_t len, int32_t flags, const Callback& cb);
	void sendAll(const void* buf, int32_t len, int32_t flags, const Callback& cb);

	// copy data from fd to this file
	int32_t sendFileFrom(int fd, int64_t offset, int32_t len);

	// copy data from fd to this file asynchronously
	void sendFileFrom(int fd, int64_t offset, int32_t len, const Callback& cb, bool repeat = false);
	void repeatSendFileFrom(int fd, int64_t offset, int32_t len, const Callback& cb);

	// may block
	virtual int close();

	// async
	virtual void close(const Callback& cb);

	void cancelRead();
	void cancelWrite();
	void cancelSend();
	void cancelRecv();
};
}
```

## EPoll
This class implements a epoll(7) based asynchronous I/O event loop. To perform any asynchronous I/O on a `File` you must register it with an `EPoll` instance using add() and call `EPoll::loop()`.

```c++
namespace CP {
class EPoll: FD {
public:
	// creates a epoll descriptor
	EPoll();

	// closes the epoll descriptor
	~EPoll();

	// add a file descriptor to epoll; this will register level triggered
	// event monitoring for both read and write, and upon events received
	// the FD's dispatch() function will be called with returned events
	// (EPOLLIN, EPOLLOUT, etc).
	// this function does not take ownership of fd, and the user must guarantee
	// fd exists until after it is removed from epoll using remove().
	void add(FD& fd);

	// immediately removes fd from epoll and cancels any outstanding event
	// notifications. After calling remove() you may delete fd.
	void remove(FD& fd);

	// run a single invocation of epoll_wait and dispatch events.
	// set timeoutMs to -1 to wait indefinitely.
	void run(int timeoutMs);

	// main event loop; calls run() with infinite timeout in a loop.
	void loop();
};
}
```

