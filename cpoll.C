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
#include <cpoll-ng/cpoll.H>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/eventfd.h>
#include <stdexcept>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sstream>
#include <sys/timerfd.h>
#include <algorithm>
#include <sys/uio.h>
#include <assert.h>
#include <signal.h>
#include <sys/sendfile.h>


namespace CP
{
	static inline int checkError(int err) {
		if (unlikely(unlikely(err < 0) && errno != EINTR && errno != EINPROGRESS))
			throw CPollException();
		return err;
	}
	static inline int checkError(int err, const char* filename) {
		if (unlikely(unlikely(err < 0) && errno != EINTR && errno != EINPROGRESS))
			throwUNIXException(filename);
		return err;
	}


	void disableSignals() {
		struct sigaction sa;
		sa.sa_handler = SIG_IGN;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART; /* Restart system calls if
		 interrupted by handler */
		sigaction(SIGHUP, &sa, NULL);
		sigaction(SIGPIPE, &sa, NULL);
		sa.sa_handler = SIG_DFL;
		//cout<<sigaction(SIGSTOP, &sa, NULL)<<endl;
		//cout<<errno<<endl;
		sigaction(SIGCONT, &sa, NULL);
		sigaction(SIGTSTP, &sa, NULL);
		sigaction(SIGTTIN, &sa, NULL);
		sigaction(SIGTTOU, &sa, NULL);
	}

	FD::FD() {
	}
	FD::FD(int fd): fd(fd) {
		setBlocking(false);
	}
	FD::~FD() {
	}
	void FD::init(int fd) {
		assert(this->fd == -1);
		this->fd = fd;
		setBlocking(false);
	}
	void FD::setBlocking(bool b) {
		int f = fcntl(fd, F_GETFL);
		if (b && (f & O_NONBLOCK)) {
			fcntl(fd, F_SETFL, f & ~O_NONBLOCK);
		} else if (!b && (f & O_NONBLOCK) == 0) {
			fcntl(fd, F_SETFL, f | O_NONBLOCK);
		}
	}

	static inline bool isWouldBlock() {
		return errno == EWOULDBLOCK || errno == EAGAIN;
	}
//File

	File::File() {
	}
	File::File(int fd): FD(fd) {
	}
	void File::init(int fd) {
		FD::init(fd);
	}
	File::File(const char* name, int flags, int perms) {
		fd = checkError(open(name, flags | O_NONBLOCK, perms), name);
	}

	int32_t File::read(void* buf, int32_t len) {
		return ::read(fd, buf, len);
	}
	int32_t File::write(const void* buf, int32_t len) {
		return ::write(fd, buf, len);
	}
	int32_t File::readv(iovec* iov, int iovcnt) {
		return ::readv(fd, iov, iovcnt);
	}
	int32_t File::writev(iovec* iov, int iovcnt) {
		return ::writev(fd, iov, iovcnt);
	}
	int32_t File::send(const void* buf, int32_t len, int32_t flags) {
		return ::send(fd, buf, len, flags);
	}
	int32_t File::recv(void* buf, int32_t len, int32_t flags) {
		return ::recv(fd, buf, len, flags);
	}
	int32_t File::sendFileFrom(int fd, int64_t offset, int32_t len) {
		off_t off = (off_t) offset;
		return (int32_t) sendfile(this->fd, fd, offset < 0 ? nullptr : &off, (size_t) len);
	}

	File::OperationResult File::doOperation(OperationInfo& op, bool isWrite, bool hup) {
		int r = -1;
		auto& rwInfo = op.info.readWrite;
		auto& rwVInfo = op.info.readWriteV;
		auto& strfInfo = op.info.sendToRecvFrom;
		auto& sfInfo = op.info.sendFile;
		Operations opType = op.operation;
		bool shortRead = false;

		op.skipSynchronousAttempt = false;

		// ordinary read/write/send/recv/readv/writev
		if(op.operation >= Operations::read && op.operation <= Operations::sendFileFrom) {
			int myLen = 0;
		repeat1:
			switch (op.operation) {
			case Operations::read:
				r = read(rwInfo.buf, rwInfo.len);					myLen = rwInfo.len;
				break;
			case Operations::write:
				r = write(rwInfo.buf, rwInfo.len);					myLen = rwInfo.len;
				break;
			case Operations::recv:
				r = recv(rwInfo.buf, rwInfo.len, rwInfo.flags);		myLen = rwInfo.len;
				break;
			case Operations::send:
				r = send(rwInfo.buf, rwInfo.len, rwInfo.flags);		myLen = rwInfo.len;
				break;
			case Operations::readv:
				r = readv(rwVInfo.iov, rwVInfo.iovcnt);				myLen = rwVInfo.lenTotal;
				break;
			case Operations::writev:
				r = writev(rwVInfo.iov, rwVInfo.iovcnt);			myLen = rwVInfo.lenTotal;
				break;
			case Operations::sendFileFrom:
				r = sendFileFrom(sfInfo.fd, sfInfo.offset, sfInfo.len);
				myLen = sfInfo.len;
				break;
			default: assert(false);
			}
			if (r < 0 && isWouldBlock()) return CONTINUE;

			// an error or eof condition occurred
			if(r <= 0) {
				op.operation = Operations::none;
				op.cb(r);
				return DONE_PERMANENT;
			}

			// we have read/written some data
			bool shortRead = (r < myLen);
			if(op.repeat) {
				op.cb(r);
				// the user has not cancelled the operation, so we need
				// to repeat it
				if(op.operation == opType) {
					// if a hup was received we can skip a syscall because
					// we know it will return 0.
					if(hup) {
						op.operation = Operations::none;
						errno = isWrite ? EPIPE : 0;
						op.cb(isWrite ? -1 : 0);
						return DONE_PERMANENT;
					}
					// if a short read/write happened we will wait for epoll
					if(shortRead)
						return CONTINUE;
					goto repeat1;
				}
				// the user cancelled the operation
				if(shortRead && !hup)
					op.skipSynchronousAttempt = true;
			} else {
				op.operation = Operations::none;
				if(shortRead && !hup) {
					//fprintf(stderr, "short read, marking fd as nonreadable\n");
					op.skipSynchronousAttempt = true;
				}
				op.cb(r);
				return DONE;
			}
		}

		// (read/write/send/recv)All
		if(op.operation >= Operations::readAll && op.operation <= Operations::writevAll) {
			// repeat is not supported for *All
			assert(!op.repeat);
			bool isrwv = (op.operation == Operations::readvAll || op.operation == Operations::writevAll);
			switch(op.operation) {
			case Operations::readAll:
				r = read(rwInfo.buf + rwInfo.lenDone, rwInfo.len - rwInfo.lenDone);
				break;
			case Operations::writeAll:
				r = write(rwInfo.buf + rwInfo.lenDone, rwInfo.len - rwInfo.lenDone);
				break;
			case Operations::sendAll:
				r = send(rwInfo.buf + rwInfo.lenDone, rwInfo.len - rwInfo.lenDone, rwInfo.flags);
				break;
			case Operations::recvAll:
				r = recv(rwInfo.buf + rwInfo.lenDone, rwInfo.len - rwInfo.lenDone, rwInfo.flags);
				break;
			case Operations::readvAll:
				r = readv(rwVInfo.iov, rwVInfo.iovcnt);
				break;
			case Operations::writevAll:
				r = writev(rwVInfo.iov, rwVInfo.iovcnt);
				break;
			default: assert(false);
			}
			if (r < 0 && isWouldBlock())
				return CONTINUE;

			int lenDone, lenTotal;
			if(isrwv) {
				lenDone = op.info.readWriteV.lenDone;
				lenTotal = op.info.readWriteV.lenTotal;
			} else {
				lenDone = op.info.readWrite.lenDone;
				lenTotal = op.info.readWrite.len;
			}

			// an error condition or eof happened
			if (r <= 0) {
				op.operation = Operations::none;
				op.cb((lenDone == 0) ? r : lenDone);
				return DONE_PERMANENT;
			}

			// data was read/written
			lenDone += r;
			if(isrwv) {
				rwVInfo.lenDone += r;
				// remove fully completed buffers from iov
				while(rwVInfo.iovcnt > 0 && rwVInfo.iov[0].iov_len <= r) {
					r -= rwVInfo.iov[0].iov_len;
					rwVInfo.iov++;
					rwVInfo.iovcnt--;
				}
				// add offset to first buffer
				if(rwVInfo.iovcnt > 0) {
					uint8_t* buf = (uint8_t*) rwVInfo.iov[0].iov_base;
					rwVInfo.iov[0].iov_base = buf + r;
					rwVInfo.iov[0].iov_len -= r;
				}
			} else {
				rwInfo.lenDone += r;
			}
			// target or eof was reached
			if (lenDone >= lenTotal || hup) {
				if(!op.repeat) op.operation = Operations::none;
				if (op.cb != nullptr)
					op.cb(lenDone);
				return DONE;
			}
			// we did not reach target, no hup occurred, and the read was a short read
			return CONTINUE;
		}

		// others
		switch (op.operation) {
			case Operations::none:
				return DONE_PERMANENT;
			case Operations::close:
				r = close();
				op.operation = Operations::none;
				op.cb(r);
				return DONE_PERMANENT;
			default: assert(false);
		}
	}
	void File::handleHup(OperationInfo& op, bool isWrite) {
		if(op.operation == Operations::none)
			return;

		// ordinary read/write/send/recv/readv/writev
		if(op.operation >= Operations::read && op.operation <= Operations::sendFileFrom) {
			op.operation = Operations::none;
			errno = isWrite ? EPIPE : 0;
			op.cb(isWrite ? -1 : 0);
		}
		// (read/write/send/recv)All
		if(op.operation >= Operations::readAll && op.operation <= Operations::recvAll) {
			op.operation = Operations::none;
			if(op.info.readWrite.lenDone > 0)
				op.cb(op.info.readWrite.lenDone);
			errno = isWrite ? EPIPE : 0;
			op.cb(isWrite ? -1 : 0);
		}
	}
	void File::dispatch(int& events) {
		// we can not define these as variables because "events" must be checked every time
		// ("events" can change between doOperation calls because the user callback may
		// cancel operations.
#define err (events & EPOLLERR)
#define hup (events & EPOLLHUP)

		if((events & EPOLLIN)) {
			doOperation(pendingOps[0], false, err || hup);
		} else if(err || hup) {
			handleHup(pendingOps[0], false);
		}
		if((events & EPOLLOUT)) {
			doOperation(pendingOps[1], true, err);
		} else if(err) {
			handleHup(pendingOps[1], true);
		}
#undef err
#undef hup
	}

	// for read/write/send/recv
	template<class FuncType, FuncType func, class... P>
	static inline bool File_tryOperation(Callback cb, bool repeat, int fd, P && ...p) {
		if(repeat) {
			while(true) {
				int r = func(fd, std::forward<P>(p)...);
				if(r <= 0) {
					if(r < 0 && isWouldBlock())
						return false;
					cb(r);
					return true;
				}
				cb(r);
			}
		}
		int r = func(fd, std::forward<P>(p)...);
		if(r >= 0 || !isWouldBlock()) {
			cb(r);
			return true;
		}
		return false;
	}

	// for readAll/writeAll/sendAll/recvAll;
	// returns bytes completed so far, or -1 if the operation is already
	// fully complete.
	template<class FuncType, FuncType func, class... P>
	int File_tryOperation2(Callback cb, int fd, void* buf, int len, P && ...p) {
		uint8_t* buf1 = (uint8_t*)buf;
		int br = 0;
		while(br < len) {
			int r = func(fd, buf1 + br, len - br, std::forward<P>(p)...);
			if(r <= 0) {
				if(r < 0 && isWouldBlock())
					return br;
				cb((br == 0) ? r : br);
				return -1;
			} else {
				br += r;
			}
		}
		cb(br);
		return -1;
	}

	void addRWOp(OperationInfo& op, Operations opType, const Callback& cb,
						void* buf, int len, int lenDone, int flags, bool repeat) {
		op.operation = opType;
		op.info.readWrite.buf = (uint8_t*) buf;
		op.info.readWrite.len = len;
		op.info.readWrite.lenDone = lenDone;
		op.repeat = repeat;
		op.cb = cb;
	}

	void File::read(void* buf, int32_t len, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[0];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already reading");
		if(!op.skipSynchronousAttempt)
			if(File_tryOperation<decltype(&::read), &::read>(cb, repeat, fd, buf, len))
				return;
		addRWOp(op, Operations::read, cb, buf, len, 0, 0, repeat);
	}
	void File::readAll(void* buf, int32_t len, const Callback& cb) {
		OperationInfo& op = pendingOps[0];
		int lenDone = 0;
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already reading");

		if(!op.skipSynchronousAttempt) {
			lenDone = File_tryOperation2<decltype(&::read), &::read>(cb, fd, buf, len);
			if(lenDone < 0) return;
		}
		addRWOp(op, Operations::readAll, cb, buf, len, lenDone, 0, false);
	}
	void File::write(const void* buf, int32_t len, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[1];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already writing");
		if(!op.skipSynchronousAttempt)
			if(File_tryOperation<decltype(&::write), &::write>(cb, repeat, fd, buf, len))
				return;
		addRWOp(op, Operations::write, cb, (void*) buf, len, 0, 0, repeat);
	}
	void File::writeAll(const void* buf, int32_t len, const Callback& cb) {
		OperationInfo& op = pendingOps[1];
		int lenDone = 0;
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already writing");

		if(!op.skipSynchronousAttempt) {
			lenDone = File_tryOperation2<decltype(&::write), &::write>(cb, fd, (void*) buf, len);
			if(lenDone < 0) return;
		}
		addRWOp(op, Operations::writeAll, cb, (void*) buf, len, lenDone, 0, false);
	}
	void File::recv(void* buf, int32_t len, int32_t flags, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[0];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already reading");
		if(!op.skipSynchronousAttempt)
			if(File_tryOperation<decltype(&::recv), &::recv>(cb, repeat, fd, buf, len, flags))
				return;
		addRWOp(op, Operations::recv, cb, buf, len, 0, flags, repeat);
	}
	void File::recvAll(void* buf, int32_t len, int32_t flags, const Callback& cb) {
		OperationInfo& op = pendingOps[0];
		int res = File_tryOperation2<decltype(&::recv), &::recv>(cb, fd, buf, len, flags);
		if(res >= 0) {
			if(op.operation != Operations::none)
				throw logic_error("this FD or file is already reading");
			op.operation = Operations::recvAll;
			op.info.readWrite.buf = (uint8_t*) buf;
			op.info.readWrite.len = len;
			op.info.readWrite.lenDone = res;
			op.repeat = false;
			op.cb = cb;
		}
	}
	void File::send(const void* buf, int32_t len, int32_t flags, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[1];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already writing");
		if(!op.skipSynchronousAttempt)
			if(File_tryOperation<decltype(&::send), &::send>(cb, repeat, fd, buf, len, flags))
				return;
		addRWOp(op, Operations::send, cb, (void*) buf, len, 0, flags, repeat);
	}
	void File::sendAll(const void* buf, int32_t len, int32_t flags, const Callback& cb) {
		OperationInfo& op = pendingOps[1];
		int res = File_tryOperation2<decltype(&::send), &::send>(cb, fd, (void*) buf, len, flags);
		if(res >= 0) {
			if(op.operation != Operations::none)
				throw logic_error("this FD or file is already writing");
			op.operation = Operations::sendAll;
			op.info.readWrite.buf = (uint8_t*) buf;
			op.info.readWrite.len = len;
			op.info.readWrite.lenDone = res;
			op.repeat = false;
			op.cb = cb;
		}
	}
	File::~File() {
		if(fd >= 0)
			close();
	}
	int File::close() {
		int ret = ::close(fd);
		fd = -1;
		return ret;
	}
	void File::close(const Callback& cb) {
		OperationInfo& op = pendingOps[1];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is still writing, can not close");

		pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLOUT;
		if(poll(&pfd, 1, 0) > 0) {
			cb(close());
		} else {
			op.operation = Operations::close;
			op.repeat = false;
			op.cb = cb;
		}
	}
	void File::cancelRead() {
		pendingOps[0].operation = Operations::none;
	}
	void File::cancelWrite() {
		pendingOps[1].operation = Operations::none;
	}
	
	int iovBytesTotal(iovec* iov, int iovcnt) {
		int ret = 0;
		for(int i=0; i<iovcnt; i++)
			ret += iov[i].iov_len;
		return ret;
	}
	void File::readv(iovec* iov, int iovcnt, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[0];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already reading");
		if(op.skipSynchronousAttempt ||
				!File_tryOperation<decltype(&::readv), &::readv>(cb, repeat, fd, iov, iovcnt))
		{
			op.operation = Operations::readv;
			op.info.readWriteV.iov = iov;
			op.info.readWriteV.iovcnt = iovcnt;
			op.info.readWriteV.lenTotal = iovBytesTotal(iov, iovcnt);
			op.repeat = repeat;
			op.cb = cb;
		}
	}
	void File::writev(iovec* iov, int iovcnt, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[1];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already writing");
		if(op.skipSynchronousAttempt ||
				!File_tryOperation<decltype(&::writev), &::writev>(cb, repeat, fd, iov, iovcnt))
		{
			op.operation = Operations::writev;
			op.info.readWriteV.iov = iov;
			op.info.readWriteV.iovcnt = iovcnt;
			op.info.readWriteV.lenTotal = iovBytesTotal(iov, iovcnt);
			op.repeat = repeat;
			op.cb = cb;
		}
	}
	void File::readvAll(iovec* iov, int iovcnt, const Callback& cb) {
		OperationInfo& op = pendingOps[0];
		op.operation = Operations::readvAll;
		op.info.readWriteV.iov = iov;
		op.info.readWriteV.iovcnt = iovcnt;
		op.info.readWriteV.lenTotal = iovBytesTotal(iov, iovcnt);
		op.info.readWriteV.lenDone = 0;
		op.repeat = false;
		op.cb = cb;
		// we will get lazy here for now and just use doOperation() to
		// do the synchronous attempt
		if(!op.skipSynchronousAttempt)
			doOperation(op, false, false);
	}
	void File::writevAll(iovec* iov, int iovcnt, const Callback& cb) {
		OperationInfo& op = pendingOps[1];
		op.operation = Operations::writevAll;
		op.info.readWriteV.iov = iov;
		op.info.readWriteV.iovcnt = iovcnt;
		op.info.readWriteV.lenTotal = iovBytesTotal(iov, iovcnt);
		op.info.readWriteV.lenDone = 0;
		op.repeat = false;
		op.cb = cb;
		// we will get lazy here for now and just use doOperation() to
		// do the synchronous attempt
		if(!op.skipSynchronousAttempt)
			doOperation(op, true, false);
	}
	void File::sendFileFrom(int fd, int64_t offset, int32_t len, const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[1];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already writing");
		if(repeat) {
			while(true) {
				int r = sendFileFrom(fd, offset, len);
				if(r <= 0) {
					if(r < 0 && isWouldBlock())
						goto addPendingOp;
					cb(r);
					return;
				}
				cb(r);
			}
		} else {
			int r = sendFileFrom(fd, offset, len);
			if(r >= 0 || !isWouldBlock()) {
				cb(r);
				return;
			}
		}
	addPendingOp:
		op.operation = Operations::sendFileFrom;
		op.info.sendFile.fd = fd;
		op.info.sendFile.offset = offset;
		op.info.sendFile.len = len;
		op.repeat = repeat;
		op.cb = cb;
	}

//Socket

	Socket::Socket(): File() {

	}
	Socket::Socket(int fd): File(fd) {
	}
	Socket::Socket(int32_t d, int32_t t, int32_t p) {
		init(d, t, p);
	}
	void Socket::init(int fd) {
		File::init(fd);
	}
	void Socket::init(int32_t d, int32_t t, int32_t p) {
		File::init(socket(d, t | SOCK_CLOEXEC | SOCK_NONBLOCK, p));
	}
	File::OperationResult Socket::doOperation(OperationInfo& op, bool isWrite, bool hup) {
		int r = -1;
		switch(op.operation) {
		case Operations::accept:
			if(op.repeat) {
				do {
					r = accept();
					if(r < 0) {
						if(isWouldBlock())
							return CONTINUE;
						op.operation = Operations::none;
						op.cb(r);
						return DONE_PERMANENT;
					}
					op.cb(r);
				} while(op.operation == Operations::accept);
				return DONE;
			} else {
				r = accept();
				if(r < 0 && isWouldBlock())
					return CONTINUE;
				op.operation = Operations::none;
				op.cb(r);
				return DONE;
			}
		case Operations::connect:
		{
			int val = 0;
			socklen_t l = sizeof(int);

			r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &val, &l);
			if(r >= 0) errno = val;
			op.operation = Operations::none;
			op.cb((errno == 0) ? 0 : -1);
			return DONE;
		}
		default:
			return File::doOperation(op, isWrite, hup);
		}
	}
	void Socket::handleHup(OperationInfo& op, bool isWrite) {
		switch(op.operation) {
		case Operations::accept:
			op.operation = Operations::none;
			errno = EPIPE;
			op.cb(-1);
			break;
		case Operations::connect:
		{
			int val = 0;
			socklen_t l = sizeof(int);

			int r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &val, &l);
			if(r >= 0) errno = val;
			op.operation = Operations::none;
			op.cb((errno == 0) ? 0 : -1);
			break;
		}
		default:
			File::handleHup(op, isWrite);
			break;
		}
	}
	void Socket::dispatch(int& events) {
		// we can not define these as variables because "events" must be checked every time
		// ("events" can change between doOperation calls because the user callback may
		// cancel operations.
#define err (events & EPOLLERR)
#define hup (events & EPOLLHUP)

		if((events & EPOLLIN)) {
			doOperation(pendingOps[0], false, err || hup);
		} else if(err || hup) {
			handleHup(pendingOps[0], false);
		}
		if((events & EPOLLOUT)) {
			doOperation(pendingOps[1], true, err);
		} else if(err) {
			handleHup(pendingOps[1], true);
		}
#undef err
#undef hup
	}
	shared_ptr<EndPoint> Socket::getLocalEndPoint() const {
		uint8_t addr[MAXSOCKADDRSIZE];
		socklen_t l = MAXSOCKADDRSIZE;
		getsockname(fd, (struct sockaddr*) addr, &l);
		assert(l <= MAXSOCKADDRSIZE);

		sockaddr* sa = (sockaddr*) addr;
		auto ep = EndPoint::create(sa->sa_family);
		ep->setSockAddr(sa);
		return ep;
	}
	shared_ptr<EndPoint> Socket::getRemoteEndPoint() const {
		uint8_t addr[MAXSOCKADDRSIZE];
		socklen_t l = MAXSOCKADDRSIZE;
		getpeername(fd, (struct sockaddr*) addr, &l);
		assert(l <= MAXSOCKADDRSIZE);

		sockaddr* sa = (sockaddr*) addr;
		auto ep = EndPoint::create(sa->sa_family);
		ep->setSockAddr(sa);
		return ep;
	}
	void Socket::bind(const sockaddr *addr, int32_t addr_size) {
		if (fd == -1)
			init(addr->sa_family, SOCK_STREAM, 0);
		int32_t tmp12345 = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &tmp12345, sizeof(tmp12345));
		if (::bind(fd, addr, addr_size) != 0)
			throw CPollException(errno);
	}
	void Socket::bind(const EndPoint &ep) {
		int32_t size = ep.getSockAddrSize();
		uint8_t tmp[size];
		ep.getSockAddr((sockaddr*) tmp);
		bind((sockaddr*) tmp, size);
	}
	void Socket::bind(const char* hostname, const char* port, int32_t family, int32_t socktype,
			int32_t proto, bool reusePort) {
		if (fd != -1) throw CPollException(
				"Socket::bind(string, ...) creates a socket, but the socket is already initialized");
		auto hosts = EndPoint::lookupHost(hostname, port, family, socktype, proto);
		unsigned int i;
		for (i = 0; i < hosts.size(); i++) {
			int sock = socket(hosts[i]->addressFamily, socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, proto);
			if (sock < 0) continue;

			int32_t tmp12345 = 1;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &tmp12345, sizeof(tmp12345));
			if(reusePort)
				setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &tmp12345, sizeof(tmp12345));

			int size = hosts[i]->getSockAddrSize();
			uint8_t tmp[size];
			hosts[i]->getSockAddr((sockaddr*) tmp);

			if (::bind(sock, (sockaddr*) tmp, size) == 0) {
				init(sock);
				return;
			} else {
				::close(sock);
				continue;
			}
		}
		throw CPollException("no bindable hosts were found; last error: " + string(strerror(errno)));
	}
	void Socket::listen(int32_t backlog) {
		checkError(::listen(fd, backlog));
	}
	int32_t Socket::shutdown(int32_t how) {
		return ::shutdown(fd, how);
	}
	void __socket_init_if_not_already(Socket* s, int32_t af) {
		if (s->fd < 0) s->init(af, SOCK_STREAM, 0);
	}
	int Socket::connect(const sockaddr *addr, int32_t addr_size) {
		__socket_init_if_not_already(this, addr->sa_family);
		return ::connect(fd, addr, addr_size);
	}
	int Socket::connect(const EndPoint &ep) {
		int32_t l = ep.getSockAddrSize();
		char tmp[l];
		ep.getSockAddr((sockaddr*) tmp);
		return connect((sockaddr*) tmp, l);
	}
	int Socket::connect(const char* hostname, const char* port, int32_t family, int32_t socktype,
			int32_t proto, int32_t flags) {
		if (fd != -1) throw CPollException(
				"Socket::connect(string, ...) creates a socket, but the socket is already initialized");
		auto hosts = EndPoint::lookupHost(hostname, port, 0, socktype, proto);
		for (int i = 0; i < hosts.size(); i++) {
			int sock = socket(hosts[i]->addressFamily, socktype | SOCK_CLOEXEC, proto);
			if (sock < 0) continue;
			int size = hosts[i]->getSockAddrSize();
			uint8_t tmp[size];
			hosts[i]->getSockAddr((sockaddr*) tmp);
			int r = ::connect(sock, (sockaddr*) tmp, size);
			if(r >= 0 || errno == EINPROGRESS) {
				init(sock);
				return r;
			} else {
				::close(sock);
				continue;
			}
		}
		throw CPollException("no reachable hosts were found; last error: " + string(strerror(errno)));
	}

	int Socket::accept() {
		int h = ::accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
		return h;
	}
	void Socket::connect(const sockaddr* addr, int32_t addr_size, const Callback& cb) {
		__socket_init_if_not_already(this, addr->sa_family);

		OperationInfo& op = pendingOps[1];
		if(op.operation != Operations::none)
			throw logic_error("this FD or file is already writing");

		int r = connect(addr, addr_size);
		if(r >= 0 || (errno != EINPROGRESS)) {
			cb(r);
			return;
		}
		op.operation = Operations::connect;
		op.repeat = false;
		op.cb = cb;
	}
	void Socket::connect(const EndPoint& ep, const Callback& cb) {
		__socket_init_if_not_already(this, ep.addressFamily);
		int32_t l = ep.getSockAddrSize();
		char tmp[l];
		ep.getSockAddr((sockaddr*) tmp);
		connect((sockaddr*) tmp, l, cb);
	}
	void Socket::accept(const Callback& cb, bool repeat) {
		OperationInfo& op = pendingOps[0];
		if(op.operation != Operations::none)
			throw logic_error("accept(): this socket is already reading");
		if(File_tryOperation<decltype(&::accept), &::accept>(cb, repeat, fd, nullptr, nullptr))
			return;
		op.operation = Operations::accept;
		op.repeat = repeat;
		op.cb = cb;
	}
	/*int32_t Socket::recvFrom(void* buf, int32_t len, int32_t flags, EndPoint& ep) {
		socklen_t size = ep.getSockAddrSize();
		uint8_t addr[size];
		int tmp = recvfrom(handle, buf, len, flags, (sockaddr*) addr, &size);
		if (tmp >= 0) ep.setSockAddr((sockaddr*) addr);
		return tmp;
	}
	int32_t Socket::sendTo(const void* buf, int32_t len, int32_t flags, const EndPoint& ep) {
		socklen_t size = ep.getSockAddrSize();
		uint8_t addr[size];
		ep.getSockAddr((sockaddr*) addr);
		int tmp = sendto(handle, buf, len, flags, (sockaddr*) addr, size);
		return tmp;
	}
	void Socket::recvFrom(void* buf, int32_t len, int32_t flags, EndPoint& ep, const Callback& cb,
			bool repeat) {
		static const Events e = Events::in;
		EventHandlerData* ed = beginAddEvent(e);
		fillIOEventHandlerData(ed, buf, len, cb, e, Operations::recvFrom);
		ed->misc.bufferIO.flags = flags;
		ed->misc.bufferIO.ep = &ep;
		endAddEvent(e, repeat);
	}
	void Socket::sendTo(const void* buf, int32_t len, int32_t flags, const EndPoint& ep,
			const Callback& cb, bool repeat) {
		static const Events e = Events::out;
		EventHandlerData* ed = beginAddEvent(e);
		fillIOEventHandlerData(ed, (void*) buf, len, cb, e, Operations::sendTo);
		ed->misc.bufferIO.flags = flags;
		ed->misc.bufferIO.const_ep = &ep;
		endAddEvent(e, repeat);
	}*/

/*
//SignalFD
	int32_t SignalFD::MAX_EVENTS(4);
	SignalFD::SignalFD(int handle, const sigset_t& mask) :
			FD(handle), mask(mask) {
	}
	SignalFD::SignalFD(const sigset_t& mask, int32_t flags) :
			FD(signalfd(-1, &mask, flags | SFD_CLOEXEC | SFD_NONBLOCK)), mask(mask) {
	}
	bool SignalFD::dispatch(Events event, const EventData& evtd, bool confident) {
		Signal sig[MAX_EVENTS];
		int32_t br = ::read(handle, sig, sizeof(sig));
		if (br < 0 && isWouldBlock()) return false;
		if (callback != nullptr) {
			br /= sizeof(Signal);
			for (int32_t i = 0; i < br; i++) {
				callback(sig[i]);
			}
		}
		return true;
	}
	Events SignalFD::getEvents() {
		return Events::in;
	}
*/

//Timer
	static void Timer_doSetInterval(Timer* This, struct timespec interval) {
		This->interval = interval;
		struct itimerspec tmp1;
		tmp1.it_interval = interval;
		tmp1.it_value = interval;
		timerfd_settime(This->fd, 0, &tmp1, NULL);
	}
	static void Timer_doSetInterval(Timer* This, uint64_t interval_ms) {
		This->interval.tv_sec = interval_ms / 1000;
		This->interval.tv_nsec = (interval_ms % 1000) * 1000000;
		struct itimerspec tmp1;
		tmp1.it_interval = This->interval;
		tmp1.it_value = This->interval;
		timerfd_settime(This->fd, 0, &tmp1, NULL);
	}
	void Timer::setInterval(struct timespec interval) {
		Timer_doSetInterval(this, interval);
	}
	void Timer::setInterval(uint64_t interval_ms) {
		Timer_doSetInterval(this, interval_ms);
	}
	void Timer::init(struct timespec interval) {
		FD::init(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
		setInterval(interval);
	}
	void Timer::init(uint64_t interval_ms) {
		FD::init(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
		setInterval(interval_ms);
	}
	void Timer::init(int handle) {
		FD::init(handle);
		struct itimerspec tmp;
		timerfd_gettime(handle, &tmp);
		interval = tmp.it_interval;
	}
	Timer::Timer(int handle) {
		init(handle);
	}
	Timer::Timer(uint64_t interval_ms) {
		init(interval_ms);
	}
	Timer::Timer(struct timespec interval) {
		init(interval);
	}
	struct timespec Timer::getInterval() {
		return interval;
	}
	bool Timer::running() {
		return !(interval.tv_nsec == 0 && interval.tv_sec == 0);
	}
	void Timer::setCallback(const Callback& cb) {
		this->cb = cb;
	}
	void Timer::dispatch(int& events) {
		if (events & EPOLLIN) {
			uint64_t tmp;
			int i = read(fd, &tmp, sizeof(tmp));
			if(i >= (int) sizeof(tmp) && cb != nullptr)
				cb((int) tmp);
		}
	}
	void Timer::close() {
		::close(fd);
		fd = -1;
	}
	Timer::~Timer() {
		if (fd < 0) return;
		close();
	}
/*
//EventFD
	EventFD::EventFD(int handle) :
			File(handle) {
	}
	EventFD::EventFD(uint32_t initval, int32_t flags) :
			File(eventfd(initval, flags | EFD_CLOEXEC | EFD_NONBLOCK)) {
	}
	bool EventFD::doOperation(Events event, EventHandlerData& ed, const EventData& evtd,
			EventHandlerData::States oldstate, bool confident) {
		int32_t r = 0;
		switch (ed.op) {
			case Operations::read:
				r = eventfd_read(handle, &ed.misc.eventfd.evt);
				break;
			case Operations::write:
				r = eventfd_write(handle, ed.misc.eventfd.evt);
				break;
			default:
				break;
		}
		if (r < 0 && isWouldBlock()) return false;
		ed.cb(r);
		return true;
	}
	eventfd_t EventFD::getEvent() {
		eventfd_t tmp;
		if (eventfd_read(handle, &tmp) == 0) return tmp;
		return -1;
	}
	void EventFD_getEventStub(EventFD* th, int i) {
		th->cb((i < 0) ? -1 : (th->eventData[eventToIndex(Events::in)].misc.eventfd.evt));
	}
	void EventFD::getEvent(const Delegate<void(eventfd_t)>& cb, bool repeat) {
		Events e = Events::in;
		EventHandlerData* ed = beginAddEvent(e);
		this->cb = cb;
		ed->cb = Callback(&EventFD_getEventStub, this);
		ed->op = Operations::read;
		endAddEvent(e, repeat);
	}
	int32_t EventFD::sendEvent(eventfd_t evt) {
		return eventfd_write(handle, evt);
	}
	void EventFD::sendEvent(eventfd_t evt, const Delegate<void(int32_t)>& cb) {
		Events e = Events::out;
		EventHandlerData* ed = beginAddEvent(e);
		ed->cb = cb;
		ed->misc.eventfd.evt = evt;
		ed->op = Operations::write;
		endAddEvent(e, false);
	}
*/


	// EPoll
	EPoll::EPoll() {
		fd = epoll_create1(EPOLL_CLOEXEC);
		checkError(fd);
		disableSignals();
	}
	EPoll::~EPoll() {
		close(fd);
	}
	void EPoll::add(FD& fd) {
		epoll_event evt = {};
		evt.events = EPOLLIN | EPOLLOUT | EPOLLET;
		evt.data.u64 = 0;
		evt.data.ptr = &fd;
		checkError(epoll_ctl(this->fd, EPOLL_CTL_ADD, fd.fd, &evt));
	}
	void EPoll::remove(FD& fd) {
		// first check pending events and remove all references to fd
		assert(currCycleNotificationsCount <= EVENTSPERCYCLE);
		for(int i=0; i<currCycleNotificationsCount; i++) {
			if(currCycleNotifications[i].data.ptr == (void*) &fd) {
				currCycleNotifications[i].data.ptr = nullptr;
				currCycleNotifications[i].events = 0;
			}
		}
		// ...then remove fd from epoll
		checkError(epoll_ctl(this->fd, EPOLL_CTL_DEL, fd.fd, nullptr));
	}
	void EPoll::run(int timeoutMs) {
		int ret = epoll_wait(fd, currCycleNotifications, EVENTSPERCYCLE, timeoutMs);
		checkError(ret);
		currCycleNotificationsCount = ret;
		assert(currCycleNotificationsCount <= EVENTSPERCYCLE);
		for(int i=0; i<currCycleNotificationsCount; i++) {
			if(currCycleNotifications[i].data.ptr != nullptr) {
				FD* inst = (FD*) currCycleNotifications[i].data.ptr;
				auto* evts = &currCycleNotifications[i].events;
				inst->dispatch(*(int*)evts);
			}
		}
		currCycleNotificationsCount = 0;
	}
	void EPoll::loop() {
		while(true)
			run(-1);
	}
	void EPoll::dispatch(int& events) {
		run(0);
	}

	PThreadMutex::PThreadMutex() {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&m, &attr);
		pthread_mutexattr_destroy(&attr);
	}
	PThreadMutex::~PThreadMutex() {
		pthread_mutex_destroy(&m);
	}
	void PThreadMutex::lock() {
		pthread_mutex_lock(&m);
	}
	void PThreadMutex::unlock() {
		pthread_mutex_unlock(&m);
	}
	void * memrmem(void *s, size_t slen, void *t, size_t tlen) {
		if (slen >= tlen) {
			size_t i = slen - tlen;
			do {
				if (memcmp((char*) s + i, (char*) t, tlen) == 0) return (char*) s + i;
			} while (i-- != 0);
		}
		return NULL;
	}


	static void* threadFunc(void* v) {
		auto* func = (function<void()>*)v;
		(*func)();
		delete func;
		return nullptr;
	}
	pthread_t createThread(const function<void()>& func) {
		auto* funcCopy = new function<void()>(func);
		pthread_t pth;
		assert(pthread_create(&pth, nullptr, &threadFunc, funcCopy) >= 0);
		return pth;
	}

	void listDirectory(const char* path, function<void(const char*)> cb) {
		DIR* d = opendir(path);
		if (d == NULL) {
			throw UNIXException(errno, path);
			return;
		}
		int len = offsetof(dirent, d_name) + pathconf(path, _PC_NAME_MAX) + 1;
		char ent[len];
		dirent* ent1 = (dirent*) ent;
		while (readdir_r(d, (dirent*) ent, &ent1) == 0 && ent1 != NULL) {
			if (strcmp(ent1->d_name, ".") == 0 || strcmp(ent1->d_name, "..") == 0) continue;
			cb(ent1->d_name);
		}
		closedir(d);
	}
}

