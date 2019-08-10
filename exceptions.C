#include <cpoll-ng/exceptions.H>
#include <string.h>
#include <errno.h>

namespace CP {
	//CPollException

	CPollException::CPollException() :
			message(strerror(errno)), number(errno) {
	}
	CPollException::CPollException(int32_t number) :
			message(strerror(number)), number(number) {
	}
	CPollException::CPollException(string message, int32_t number) :
			message(message), number(number) {
	}
	CPollException::~CPollException() throw () {
	}
	const char* CPollException::what() const throw () {
		return message.c_str();
	}

	UNIXException::UNIXException() :
			runtime_error(strerror(errno)), number(errno) {
	}
	UNIXException::UNIXException(int32_t number) :
			runtime_error(strerror(number)), number(number) {
	}
	UNIXException::UNIXException(int32_t number, string objName) :
			runtime_error(string(strerror(number)) + ": " + objName), number(number) {
	}
	UNIXException::~UNIXException() throw () {
	}

	FileNotFoundException::FileNotFoundException() :
			UNIXException(ENOENT) {
	}
	FileNotFoundException::FileNotFoundException(string filename) :
			UNIXException(ENOENT, filename) {
	}
	FileNotFoundException::~FileNotFoundException() throw () {
	}
	void throwUNIXException() {
		if (errno == ENOENT) throw FileNotFoundException();
		throw UNIXException(errno);
	}
	void throwUNIXException(string fn) {
		if (errno == ENOENT) throw FileNotFoundException(fn);
		throw UNIXException(errno, fn);
	}

	AbortException::AbortException() {
	}
	AbortException::~AbortException() throw () {
	}
	const char* AbortException::what() const throw () {
		return "aborting cpoll loop";
	}

	CancelException::CancelException() {
	}
	CancelException::~CancelException() throw () {
	}
	const char* CancelException::what() const throw () {
		return "cancelling current cpoll operation";
	}
}
