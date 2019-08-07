#include <cpoll-ng/ipaddress.H>
#include <cpoll-ng/exceptions.H>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string>
#include <sstream>

using namespace std;

namespace CP {
	string IPAddress::toStr() const {
		char tmp[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &a, tmp, INET_ADDRSTRLEN) == NULL) throw CPollException();
		return string(tmp);
	}
	string IPv6Address::toStr() const {
		char tmp[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET6, &a, tmp, INET6_ADDRSTRLEN) == NULL) throw CPollException();
		return string(tmp);
	}
	vector<shared_ptr<EndPoint> > EndPoint::lookupHost(const char* hostname, const char* port,
			int32_t family, int32_t socktype, int32_t proto, int32_t flags) {
		vector<shared_ptr<EndPoint> > tmp;
		addrinfo hints, *result, *rp;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = family; /* Allow IPv4 or IPv6 */
		hints.ai_socktype = socktype;
		hints.ai_flags = flags;
		hints.ai_protocol = proto;

		int32_t s = getaddrinfo(hostname, port, &hints, &result);
		if (s != 0) {
			throw CPollException(gai_strerror(s));
		}
		for (rp = result; rp != NULL; rp = rp->ai_next) {
			auto ep = fromSockAddr(rp->ai_addr);
			tmp.push_back(ep);
		}
		freeaddrinfo(result);
		return tmp;
	}
	shared_ptr<EndPoint> EndPoint::fromSockAddr(const sockaddr* addr) {
		switch (addr->sa_family) {
			case AF_INET:
				return make_shared<IPEndPoint>(*((sockaddr_in*) addr));
			case AF_INET6:
				return make_shared<IPv6EndPoint>(*((sockaddr_in6*) addr));
			case AF_UNIX:
				return make_shared<UNIXEndPoint>(*((sockaddr_un*) addr));
			default:
				return nullptr;
		}
	}
	shared_ptr<EndPoint> EndPoint::create(int32_t addressFamily) {
		switch (addressFamily) {
			case AF_INET:
				return make_shared<IPEndPoint>();
			case AF_INET6:
				return make_shared<IPv6EndPoint>();
			case AF_UNIX:
				return make_shared<UNIXEndPoint>();
			default:
				return nullptr;
		}
	}
	int EndPoint::getSize(int32_t addressFamily) {
		switch (addressFamily) {
			case AF_INET:
				return sizeof(IPEndPoint);
			case AF_INET6:
				return sizeof(IPv6EndPoint);
			case AF_UNIX:
				return sizeof(UNIXEndPoint);
			default:
				return 0;
		}
	}
	EndPoint* EndPoint::construct(void* mem, int32_t addressFamily) {
		switch (addressFamily) {
			case AF_INET:
				return new (mem) IPEndPoint;
			case AF_INET6:
				return new (mem) IPv6EndPoint;
			case AF_UNIX:
				return new (mem) UNIXEndPoint;
			default:
				return NULL;
		}
	}

	//IPEndPoint

	IPEndPoint::IPEndPoint() {
		this->addressFamily = AF_INET;
	}
	IPEndPoint::IPEndPoint(IPAddress address, in_port_t port) {
		this->addressFamily = AF_INET;
		this->address = address;
		this->port = port;
	}
	void IPEndPoint::set_addr(const sockaddr_in& addr) {
		this->addressFamily = AF_INET;
		this->address = IPAddress(addr.sin_addr);
		this->port = ntohs(addr.sin_port);
	}
	void IPEndPoint::setSockAddr(const sockaddr* addr) {
		if (addr->sa_family != AF_INET) throw CPollException(
				"attemting to set the address of an IPEndPoint to a sockaddr that is not AF_INET");
		set_addr(*(sockaddr_in*) addr);
	}
	IPEndPoint::IPEndPoint(const sockaddr_in& addr) {
		set_addr(addr);
	}
	void IPEndPoint::getSockAddr(sockaddr* addr) const {
		sockaddr_in* addr_in = (sockaddr_in*) addr;
		addr_in->sin_family = AF_INET;
		addr_in->sin_port = htons(port);
		addr_in->sin_addr = address.a;
	}
	int32_t IPEndPoint::getSockAddrSize() const {
		return sizeof(sockaddr_in);
	}
	void IPEndPoint::clone(EndPoint& to) const {
		if (to.addressFamily != addressFamily) throw CPollException(
				"attempting to clone an EndPoint to another EndPoint with a different addressFamily");
		IPEndPoint& tmp((IPEndPoint&) to);
		tmp.address = address;
		tmp.port = port;
	}
	string IPEndPoint::toStr() const {
		stringstream s;
		s << address.toStr() << ':' << port;
		return s.str();
	}

	//IPv6EndPoint

	IPv6EndPoint::IPv6EndPoint() {
		this->addressFamily = AF_INET6;
	}
	IPv6EndPoint::IPv6EndPoint(IPv6Address address, in_port_t port) {
		this->addressFamily = AF_INET6;
		this->address = address;
		this->port = port;
	}
	void IPv6EndPoint::set_addr(const sockaddr_in6& addr) {
		this->addressFamily = AF_INET6;
		this->address = IPv6Address(addr.sin6_addr);
		this->port = ntohs(addr.sin6_port);
		flowInfo = addr.sin6_flowinfo;
		scopeID = addr.sin6_scope_id;
	}
	IPv6EndPoint::IPv6EndPoint(const sockaddr_in6& addr) {
		set_addr(addr);
	}
	void IPv6EndPoint::setSockAddr(const sockaddr* addr) {
		if (addr->sa_family != AF_INET6) throw CPollException(
				"attemting to set the address of an IPv6EndPoint to a sockaddr that is not AF_INET6");
		set_addr(*(sockaddr_in6*) addr);
	}
	void IPv6EndPoint::getSockAddr(sockaddr* addr) const {
		sockaddr_in6* addr_in = (sockaddr_in6*) addr;
		addr_in->sin6_family = AF_INET6;
		addr_in->sin6_port = htons(port);
		addr_in->sin6_addr = address.a;
		addr_in->sin6_flowinfo = flowInfo;
		addr_in->sin6_scope_id = scopeID;
	}
	int32_t IPv6EndPoint::getSockAddrSize() const {
		return sizeof(sockaddr_in6);
	}
	void IPv6EndPoint::clone(EndPoint& to) const {
		if (to.addressFamily != addressFamily) throw CPollException(
				"attempting to clone an EndPoint to another EndPoint with a different addressFamily");
		IPv6EndPoint& tmp((IPv6EndPoint&) to);
		tmp.address = address;
		tmp.port = port;
		tmp.flowInfo = flowInfo;
		tmp.scopeID = scopeID;
	}
	string IPv6EndPoint::toStr() const {
		stringstream s;
		s << '[' << address.toStr() << "]:" << port;
		return s.str();
	}

	//UNIXEndPoint
	UNIXEndPoint::UNIXEndPoint() {
		this->addressFamily = AF_UNIX;
	}
	UNIXEndPoint::UNIXEndPoint(string name) {
		this->addressFamily = AF_UNIX;
		this->name = name;
	}
	void UNIXEndPoint::set_addr(const sockaddr_un& addr) {
		this->addressFamily = AF_UNIX;
		this->name = addr.sun_path;
	}
	UNIXEndPoint::UNIXEndPoint(const sockaddr_un& addr) {
		set_addr(addr);
	}
	void UNIXEndPoint::setSockAddr(const sockaddr* addr) {
		if (addr->sa_family != AF_UNIX) throw CPollException(
				"attemting to set the address of an UNIXEndPoint to a sockaddr that is not AF_UNIX");
		set_addr(*(sockaddr_un*) addr);
	}
	void UNIXEndPoint::getSockAddr(sockaddr* addr) const {
		sockaddr_un* a = (sockaddr_un*) addr;
		a->sun_family = AF_UNIX;
		strncpy(a->sun_path, name.c_str(), name.length());
		a->sun_path[name.length()] = '\0';
	}
	int32_t UNIXEndPoint::getSockAddrSize() const {
		return sizeof(sa_family_t) + name.length() + 1;
	}
	void UNIXEndPoint::clone(EndPoint& to) const {
		if (to.addressFamily != addressFamily) throw CPollException(
				"attempting to clone an EndPoint to another EndPoint with a different addressFamily");
		UNIXEndPoint& tmp((UNIXEndPoint&) to);
		tmp.name = name;
	}
	string UNIXEndPoint::toStr() const {
		return name;
		//XXX
	}
}
