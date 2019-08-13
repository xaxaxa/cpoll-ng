#include <arpa/inet.h>
#include <stdint.h>
#include <sys/un.h>
#include <memory>
#include <vector>
#include <string>

namespace CP {
	using std::string;
	using std::shared_ptr;
	using std::vector;
	static inline uint64_t ntohll(uint64_t value) {
		// The answer is 42
		static const int32_t num = 42;

		// Check the endianness
		if (*reinterpret_cast<const char*>(&num) == num) {
			const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
			const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));

			return (static_cast<uint64_t>(low_part) << 32) | high_part;
		} else {
			return value;
		}
	}

	//==============================================================
	//==============================================================
	//=================NETWORK UTILITY CLASSES======================
	//==============================================================
	//==============================================================
	
	struct IPAddress
	{
		in_addr a;
		inline IPAddress() {
		}
		inline IPAddress(const char* addr/*NOT hostname*/) {
			inet_pton(AF_INET, addr, &a.s_addr);
		}
		inline IPAddress(const in_addr& a) {
			this->a = a;
		}
		inline bool operator<(const IPAddress& other) const {
			return ntohl(a.s_addr) < ntohl(other.a.s_addr);
		}
		inline bool operator>(const IPAddress& other) const {
			return ntohl(a.s_addr) > ntohl(other.a.s_addr);
		}
		inline bool operator<=(const IPAddress& other) const {
			return ntohl(a.s_addr) <= ntohl(other.a.s_addr);
		}
		inline bool operator>=(const IPAddress& other) const {
			return ntohl(a.s_addr) >= ntohl(other.a.s_addr);
		}
		inline bool operator==(const IPAddress& other) const {
			return a.s_addr == other.a.s_addr;
		}
		inline IPAddress operator+(const IPAddress& other) const {
			return IPAddress( { htonl(ntohl(a.s_addr) + ntohl(other.a.s_addr)) });
		}
		inline IPAddress operator-(const IPAddress& other) const {
			return IPAddress( { htonl(ntohl(a.s_addr) - ntohl(other.a.s_addr)) });
		}
		inline IPAddress operator+(int i) const {
			return IPAddress( { htonl(ntohl(a.s_addr) + i) });
		}
		inline IPAddress operator-(int i) const {
			return IPAddress( { htonl(ntohl(a.s_addr) - i) });
		}
		string toStr() const;
	};
	struct IPv6Address
	{
		in6_addr a;
		inline IPv6Address() {
		}
		inline IPv6Address(const char* addr) {
			inet_pton(AF_INET6, addr, &a.__in6_u);
		}
		inline IPv6Address(const in6_addr& a) {
			this->a = a;
		}
		string toStr() const;
	};
	class EndPoint
	{
	public:
		int32_t addressFamily;
		virtual void getSockAddr(sockaddr* addr) const=0;
		virtual void setSockAddr(const sockaddr* addr)=0;
		virtual int32_t getSockAddrSize() const=0;
		static shared_ptr<EndPoint> fromSockAddr(const sockaddr* addr);
		static shared_ptr<EndPoint> create(int32_t addressFamily);
		static int getSize(int32_t addressFamily);
		EndPoint* construct(void* mem, int32_t addressFamily);
		virtual void clone(EndPoint& to) const=0;
		virtual ~EndPoint() {
		}
		static vector<shared_ptr<EndPoint> > lookupHost(const char* hostname, const char* port,
				int32_t family = AF_UNSPEC, int32_t socktype = 0, int32_t proto = 0, int32_t flags = 0);
		//static EndPoint Resolve(
		virtual string toStr() const=0;
	};
	class IPEndPoint: public EndPoint
	{
	public:
		IPAddress address;
		in_port_t port;
		IPEndPoint();
		IPEndPoint(IPAddress address, in_port_t port);
		void set_addr(const sockaddr_in& addr);
		virtual void setSockAddr(const sockaddr* addr);
		IPEndPoint(const sockaddr_in& addr);
		virtual void getSockAddr(sockaddr* addr) const;
		virtual int32_t getSockAddrSize() const;
		virtual void clone(EndPoint& to) const;
		virtual string toStr() const;
	};
	class IPv6EndPoint: public EndPoint
	{
	public:
		IPv6Address address;
		in_port_t port;
		uint32_t flowInfo;
		uint32_t scopeID;
		IPv6EndPoint();
		IPv6EndPoint(IPv6Address address, in_port_t port);
		void set_addr(const sockaddr_in6& addr);
		IPv6EndPoint(const sockaddr_in6& addr);
		virtual void setSockAddr(const sockaddr* addr);
		virtual void getSockAddr(sockaddr* addr) const;
		virtual int32_t getSockAddrSize() const;
		virtual void clone(EndPoint& to) const;
		virtual string toStr() const;
	};
	class UNIXEndPoint: public EndPoint
	{
	public:
		string name;
		UNIXEndPoint();
		UNIXEndPoint(string name);
		void set_addr(const sockaddr_un& addr);
		UNIXEndPoint(const sockaddr_un& addr);
		virtual void setSockAddr(const sockaddr* addr);
		virtual void getSockAddr(sockaddr* addr) const;
		virtual int32_t getSockAddrSize() const;
		virtual void clone(EndPoint& to) const;
		virtual string toStr() const;
	};
}
