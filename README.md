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
