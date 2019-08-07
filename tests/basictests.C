#include <cpoll-ng/cpoll.H>
#include <assert.h>
#include <unistd.h>
using namespace CP;
using namespace std;


void* threadFunc(void* v) {
	auto* func = (function<void()>*)v;
	(*func)();
	delete func;
	return nullptr;
}
void createThread(const function<void()>& func) {
	auto* funcCopy = new function<void()>(func);
	pthread_t pth;
	assert(pthread_create(&pth, nullptr, &threadFunc, funcCopy) >= 0);
}

int createPipeSource() {
	static int pipeNum = 0;
	int pipefd[2];

	int s1 = rand(), s2 = rand(), s3 = rand();
	pipeNum++;
	assert(pipe(pipefd) >= 0);

	int wfd = pipefd[1];
	int pN = pipeNum;
	createThread([wfd, pN, s1, s2, s3]() {
		drand48_data randState;
		uint16_t seeds[3] = {(uint16_t)s1, (uint16_t)s2, (uint16_t)s3};
		seed48_r(seeds, &randState);
		
		for(int i=0; i<10; i++) {
			char buf[128];
			int l = snprintf(buf, sizeof(buf), "source %d packet %d\n", pN, i);
			write(wfd, buf, l);
			
			double r = 0;
			drand48_r(&randState, &r);
			usleep(100000*r);
		}
		close(wfd);
	});
	return pipefd[0];
}

struct readStreamAndPrint {
	char buf[256];
	File f;
	function<void()> completedCB;
	void start(int fd) {
		f.init(fd);
		doRead();
	}
	void doRead() {
		f.read(buf, sizeof(buf), [this](int r) {
			readCB(r);
		});
	}
	void readCB(int r) {
		if(r <= 0) {
			completedCB();
			return;
		}
		write(1, buf, r);
		doRead();
	}
};

int main() {
	fprintf(stderr, "sizeof(File) = %d\n", (int) sizeof(File));
	fprintf(stderr, "sizeof(Callback) = %d\n", (int) sizeof(Callback));

	int nStreams = 5;
	EPoll ep;
	readStreamAndPrint rs[nStreams];
	int nActive = nStreams;

	auto completedCB = [&]() {
		nActive--;
		if(nActive == 0)
			exit(0);
	};

	for(int i=0; i<nStreams; i++) {
		rs[i].completedCB = completedCB;
		int p = createPipeSource();
		rs[i].start(p);
		ep.add(rs[i].f);
	}
	ep.loop();
}
