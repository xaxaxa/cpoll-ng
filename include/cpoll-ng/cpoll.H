/*
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * */

/*
 * uses code from http://www.cse.yorku.ca/~oz/hash.html
 * for sdbm hash algorithm
 * */

#ifndef __INCLUDED_CPOLL_H
#define __INCLUDED_CPOLL_H
#include <cpoll-ng/exceptions.H>
#include <cpoll-ng/ipaddress.H>
#include <functional>
#include <sys/epoll.h>

#ifndef WARNLEVEL
#define WARNLEVEL 5
#endif
#ifndef WARN
#define WARN(LEVEL,MSG) if(LEVEL<=WARNLEVEL){if(LEVEL<=1)cerr << "\x1B[41;1;33m"; else if(LEVEL<=2)cerr << "\x1B[1;1;1m"; cerr << MSG << "\x1B[0;0;0m" << endl;}
#endif
/*
 * CPoll: low level c++ wrapper for poll() and epoll(); can be implemented on
 * other OS's using mechanisms such as overlapped I/O(windows), but currently
 * there is only a linux implementation; for other OS's patches are welcome ;)
 *
 * simple usage example:
 *
 * char buf[4096];
 * char buf2[4096];
 * File f(1); //stdout
 * File f2(2); //stderr
 * f.read(buf, 4096, [](){cout << "read1 done" << endl;});
 * f2.read(buf2, 4096, [](){cout << "read2 done" << endl;});
 *
 * Poll p;
 * p.add(f);
 * p.add(f2);
 * p.loop(); //epoll is used
 *
 *
 *
 *
 * nested example:
 *
 * File f(0); //stdin
 * char buf[4096];
 * f.read([](){}, buf,4096);
 * Poll poll;
 * poll.add(f);
 * Poll poll2;
 * poll2.add(poll);
 * poll2.loop();
 */

#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#endif

namespace CP
{
	using namespace std;
	typedef function<void(int)> Callback;

	void disableSignals();

	//===========================================================
	//======================ABSTRACT CLASSES=====================
	//===========================================================
	

	//===========================================================
	//======================UTILITY CLASSES======================
	//===========================================================

	//===========================================================
	//======================DATA STRUCTURES======================
	//===========================================================

	enum class Operations: uint8_t {
		none = 0,
		read = 1, write, send, recv, readv, writev, sendFileFrom,
		readAll, writeAll, sendAll, recvAll, readvAll, writevAll,
		sendTo, recvFrom,
		accept, connect, close, lastItem
	};

	// stores the operation type and info of an asynchronous read or write
	struct OperationInfo
	{
	public:
		Callback cb;
		union {
			struct {
				uint8_t* buf;
				int32_t len;
				int32_t lenDone;
				int32_t flags;
			} readWrite;

			struct {
				uint8_t* buf;
				EndPoint* ep;
				int32_t len;
				int32_t flags;
			} sendToRecvFrom;

			struct {
				iovec* iov;
				int iovcnt;
				int lenTotal;

				// for readvAll/writevAll
				int lenDone;
			} readWriteV;

			struct {
				uint64_t evt;
			} eventfd;

			struct {
				int64_t offset;
				int fd;
				int32_t len;
			} sendFile;
		} info;
		Operations operation = Operations::none;
		bool repeat = false;
		bool skipSynchronousAttempt = false;
	};

	//============================================================
	//============================================================
	//=======================MAIN CLASSES=========================
	//============================================================
	//============================================================
	
	class FD
	{
	public:
		int fd = -1;

		FD();
		FD(int fd);
		FD(const FD& other) = delete;
		FD& operator=(const FD& other) = delete;
		void init(int fd);

		// the pass-by-reference here is important; because dispatch()
		// may call user callbacks, and the user may alter epoll status
		// (e.g. deregister fd from epoll), during dispatch() "events"
		// may change.
		virtual void dispatch(int& events)=0;

		void setBlocking(bool b);
		~FD();
	};
	class File: public FD
	{
	public:
		// 0 is read, 1 is write
		OperationInfo pendingOps[2];

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

		// The following functions are equivalent to the POSIX functions with
		// the same name. These functions should be assumed to always be nonblocking
		// because the constructors always set nonblocking mode on all file descriptors.
		int32_t read(void* buf, int32_t len);
		int32_t readv(iovec* iov, int iovcnt);
		int32_t write(const void* buf, int32_t len);
		int32_t writev(iovec* iov, int iovcnt);
		int32_t send(const void* buf, int32_t len, int32_t flags = 0);
		int32_t recv(void* buf, int32_t len, int32_t flags = 0);


		// async functions: these perform the same operation as the POSIX
		// functions with the same name, while passing the return value (int)
		// to the callback cb and setting errno to the error returned by the OS.

		// if repeat is set to true, the operation will be repeated with
		// the same arguments until an error or EOF condition happens.
		// The callback will be called once for each attempt.

		void read(void* buf, int32_t len, const Callback& cb, bool repeat = false);
		void readv(iovec* iov, int iovcnt, const Callback& cb, bool repeat = false);
		void write(const void* buf, int32_t len, const Callback& cb, bool repeat = false);
		void writev(iovec* iov, int iovcnt, const Callback& cb, bool repeat = false);

		// similar to read/write, except these will attempt to fully read/write the buffer
		// or until an error or EOF condition is encountered, in which case the callback
		// is called with the number of bytes read/written so far
		// (or -1 if an error occurred on the first attempt).
		void readAll(void* buf, int32_t len, const Callback& cb);
		void writeAll(const void* buf, int32_t len, const Callback& cb);

		void readvAll(iovec* iov, int iovcnt, const Callback& cb);
		void writevAll(iovec* iov, int iovcnt, const Callback& cb);

		// functionally identical to read/write/readAll/writeAll, but uses
		// the send/recv syscalls.
		void recv(void* buf, int32_t len, int32_t flags, const Callback& cb, bool repeat = false);
		void send(const void* buf, int32_t len, int32_t flags, const Callback& cb, bool repeat = false);
		void recvAll(void* buf, int32_t len, int32_t flags, const Callback& cb);
		void sendAll(const void* buf, int32_t len, int32_t flags, const Callback& cb);

		inline void repeatRead(void* buf, int32_t len, const Callback& cb) {
			read(buf, len, cb, true);
		}
		inline void repeatWrite(const void* buf, int32_t len, const Callback& cb) {
			write(buf, len, cb, true);
		}
		inline void repeatRecv(void* buf, int32_t len, int32_t flags, const Callback& cb) {
			recv(buf, len, flags, cb, true);
		}
		inline void repeatSend(const void* buf, int32_t len, int32_t flags, const Callback& cb) {
			send(buf, len, flags, cb, true);
		}

		// all integers representing on-disk offsets and lengths are 64 bit;
		// integers representing in-memory offsets and lengths are 32 bit.
		int32_t sendFileFrom(int fd, int64_t offset, int32_t len);

		void sendFileFrom(int fd, int64_t offset, int32_t len, const Callback& cb, bool repeat = false);
		void repeatSendFileFrom(int fd, int64_t offset, int32_t len, const Callback& cb) {
			return sendFileFrom(fd, offset, len, cb, true);
		}

		// may block
		virtual int close();

		// async
		virtual void close(const Callback& cb);

		void cancelRead();
		void cancelWrite();
		void cancelSend() {
			cancelWrite();
		}
		void cancelRecv() {
			cancelRead();
		}

		// INTERNAL FUNCTIONS

		enum OperationResult {
			CONTINUE,		// the operation is incomplete
			DONE,			// the operation is complete, and may be repeated
			DONE_PERMANENT	// the operation is complete and must not be repeated
		};

		// performs the operation specified in op.
		OperationResult doOperation(OperationInfo& op, bool isWrite, bool hup);

		void handleHup(OperationInfo& op, bool isWrite);

		void dispatch(int& events) override;
	};
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

		virtual void dispatch(int& events) override;
	protected:
		static constexpr int EVENTSPERCYCLE = 32;
		epoll_event currCycleNotifications[EVENTSPERCYCLE];
		int currCycleNotificationsCount = 0;
	};

	class Socket: public File
	{
	public:
		static const int MAXSOCKADDRSIZE = 256;
		Socket();
		Socket(int fd);
		Socket(int32_t d, int32_t t, int32_t p = 0);
		virtual void init(int fd);
		virtual void init(int32_t d, int32_t t, int32_t p = 0);

		shared_ptr<EndPoint> getLocalEndPoint() const;
		shared_ptr<EndPoint> getRemoteEndPoint() const;


		int32_t recvFrom(void* buf, int32_t len, int32_t flags, EndPoint& ep);
		int32_t sendTo(const void* buf, int32_t len, int32_t flags, const EndPoint& ep);

		int connect(const sockaddr *addr, int32_t addr_size);
		int connect(const EndPoint &ep);
		int connect(const char* hostname, const char* port, int32_t family = AF_UNSPEC,
				int32_t socktype = SOCK_STREAM, int32_t proto = 0, int32_t flags = 0);

		void bind(const sockaddr *addr, int32_t addr_size);
		void bind(const EndPoint &ep);

		//initsock is called right after creating the socket; you can use it to set socket options
		//such as SO_REUSEPORT that need to be set before binding
		void bind(const char* hostname, const char* port, int32_t family = AF_UNSPEC,
				int32_t socktype = SOCK_STREAM, int32_t proto = 0, bool reusePort = true);

		void listen(int32_t backlog = 16);
		int accept();
		int32_t shutdown(int32_t how);


		// async functions


		// ep has to remain valid for the entire duration of the request
		void recvFrom(void* buf, int32_t len, int32_t flags, EndPoint& ep,
				const Callback& cb, bool repeat = false);
		void sendTo(const void* buf, int32_t len, int32_t flags, const EndPoint& ep,
				const Callback& cb, bool repeat = false);

		inline void repeatRecvFrom(void* buf, int32_t len, int32_t flags, EndPoint& ep, const Callback& cb) {
			recvFrom(buf, len, flags, ep, cb, true);
		}
		inline void repeatSendTo(const void* buf, int32_t len, int32_t flags, const EndPoint& ep, const Callback& cb) {
			sendTo(buf, len, flags, ep, cb, true);
		}

		void connect(const sockaddr *addr, int32_t addr_size, const Callback& cb);
		void connect(const EndPoint &ep, const Callback& cb);
		void connect(const char* hostname, const char* port, const Callback& cb, int32_t family =
				AF_UNSPEC, int32_t socktype = 0, int32_t proto = 0, int32_t flags = 0);

		void accept(const Callback& cb, bool repeat = false);
		inline void repeatAccept(const Callback& cb) {
			accept(cb, true);
		}


		// performs the operation specified in op.
		OperationResult doOperation(OperationInfo& op, bool isWrite, bool hup);
		void handleHup(OperationInfo& op, bool isWrite);
		void dispatch(int& events) override;
	};
	
	class Timer: public FD
	{
	public:
		Callback cb;
		struct timespec interval;

		//if interval is 0, timer is disabled.
		// timer is always recurring (unless disabled)
		void setInterval(struct timespec interval);
		void setInterval(uint64_t interval_ms);
		void init(int fd);
		void init(struct timespec interval);
		void init(uint64_t interval_ms = 0);
		void close();
		Timer(int fd);
		Timer(uint64_t interval_ms = 0);
		Timer(struct timespec interval);
		~Timer();
		struct timespec getInterval();
		bool running();
		void setCallback(const Callback& cb);
		virtual void dispatch(int& events) override;
	};


	//all mutexes are assumed to be recursive
	class Mutex
	{
	public:
		virtual void lock()=0;
		virtual void unlock()=0;
	};
	class ScopeLock
	{
	public:
		Mutex* mutex;
		ScopeLock(Mutex& m) :
				mutex(&m) {
			m.lock();
		}
		ScopeLock(const ScopeLock& l) = delete;
		void earlyUnlock() {
			if (mutex != nullptr) mutex->unlock();
			mutex = nullptr;
		}
		~ScopeLock() {
			if (mutex != nullptr) mutex->unlock();
		}
		ScopeLock& operator=(const ScopeLock& other) = delete;
	};
	class PThreadMutex: public Mutex
	{
	public:
		pthread_mutex_t m;
		PThreadMutex();
		~PThreadMutex();
		void lock() override;
		void unlock() override;
	};

	pthread_t createThread(const function<void()>& func);
	void listDirectory(const char* path, function<void(const char*)> cb);
}
static unsigned long sdbm(uint8_t* str, int len) {
	unsigned long hash = 0;
	int c;
	for (int i = 0; i < len; i++) {
		c = str[i];
		hash = c + (hash << 6) + (hash << 16) - hash;
	}
	return hash;
}
#endif
