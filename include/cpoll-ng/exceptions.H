#include <stdexcept>
#include <string>
using namespace std;
namespace CP
{
	class CPollException: public std::exception
	{
	public:
		string message;
		int32_t number;
		CPollException();
		CPollException(int32_t number);
		CPollException(string message, int32_t number = 0);
		~CPollException() throw ();
		const char* what() const throw ();
	};
	class UNIXException: public std::runtime_error
	{
	public:
		int32_t number;
		UNIXException();
		UNIXException(int32_t number);
		UNIXException(int32_t number, string objName);
		~UNIXException() throw ();
	};
	class FileNotFoundException: public UNIXException
	{
	public:
		FileNotFoundException();
		FileNotFoundException(string filename);
		~FileNotFoundException() throw ();
	};
	void throwUNIXException();
	void throwUNIXException(string filename);
	class AbortException: public std::exception
	{ //used for aborting the event loop
	public:
		AbortException();
		~AbortException() throw ();
		const char* what() const throw ();
	};
	class CancelException: public std::exception
	{ //used for cancelling a repeat[Read|Write] operation
	  //do NOT throw this exception on one-shot operations such as read()
	public:
		CancelException();
		~CancelException() throw ();
		const char* what() const throw ();
	};
}
