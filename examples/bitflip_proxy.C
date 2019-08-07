#include <iostream>
#include <cpoll-ng/cpoll.H>
#include <signal.h>

//name is wrong; it does bitshift not bitflip

using namespace CP;

static const int bufSize=4096;

EPoll ep;

int main(int argc, char** argv)
{
	if(argc<5) {
		cerr << "usage: " << argv[0] << " bind_host bind_port forward_host forward_port [r]" << endl;
		return 1;
	}
	//no need to disable SIGHUP and SIGPIPE as cpoll does that automatically

	Socket srvsock;
	srvsock.bind(argv[1], argv[2]);
	ep.add(srvsock);

	struct conf {
		IPEndPoint fwd;
		bool r;
	} cfg {{IPAddress(argv[3]), (in_port_t)atoi(argv[4])}, argc>5&&argv[5][0]=='r'};

	auto handleConnection = [&cfg](int sock) {
		struct handler {
			conf& cfg;
			Socket s;
			Socket s2;
			char buf1[bufSize];	//local -> remote
			char buf2[bufSize]; //remote -> local
			bool rev;
			handler(conf& cfg, int sock)
				:cfg(cfg), s(sock), s2(AF_INET,SOCK_STREAM,0) {
			}
			void stop() {
				ep.remove(s);
				ep.remove(s2);
				delete this;
			}
			void closed(int i) {
				stop();
			}
			void transform(uint8_t* buf, int len) {
				if(cfg.r)
					for(int i=0;i<len;i++)
						buf[i]-=69;
				else
					for(int i=0;i<len;i++)
						buf[i]+=69;
			}
			void start() {
				ep.add(s);
				ep.add(s2);
				s2.connect(cfg.fwd, [this](int r) { connectCB(r); });
			}
			void read1() {
				s.recv(buf1,bufSize,0, [this](int r) { read1cb(r); });
			}
			void read2() {
				s2.recv(buf2,bufSize,0,[this](int r) { read2cb(r); });
			}
			void read1cb(int r) {
				if(r<=0) {
					s2.close([this](int r) { closed(r); });
					return;
				}
				transform((uint8_t*)buf1,r);
				write1(r);
			}
			void read2cb(int r) {
				if(r<=0) { s.close([this](int r) { closed(r); }); return; }
				transform((uint8_t*)buf2,r);
				write2(r);
			}
			
			void write1(int i) { s2.sendAll(buf1,i,0,[this](int r) { write1cb(r); }); }
			void write2(int i) { s.sendAll(buf2,i,0,[this](int r) { write2cb(r); }); }
			void write1cb(int r) {
				if(r<=0) { stop(); return; }
				read1();
			}
			void write2cb(int r) {
				if(r<=0) { stop(); return; }
				read2();
			}
			void connectCB(int r) {
				//printf("%i\n",r);
				if(r<0) { stop(); return; }
				read1();
				read2();
			}
		}* hdlr=new handler(cfg,sock);
		hdlr->start();
	};

	srvsock.listen();
	srvsock.repeatAccept(handleConnection);
	ep.loop();
}
