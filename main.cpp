#include <iostream>
#include <set>
#include <string>
#include <regex>
#include <cstdlib>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <getopt.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_EVENTS 32
struct epoll_event Events[MAX_EVENTS];
char *directory;

int set_nonblock(int fd)
{
	int flags;
#if defined(O_NONBLOCK)
	if(-1 == (flags == fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	flags = 1;
	return ioctl(fd, FIONBIO, &flags);
#endif
}

/*std::string http_parse(std::string& request)
{
	FILE *f = fopen("/home/box/regex.log", "a");
	fprintf(f, "%s\n", request.c_str());
	std::smatch m;
	std::regex e("GET \/([\\w\.]*)([\?\\w\=\&]+|) HTTP\/[\\d\.]+");   // GET \/([\w\.]*)([\?\w=\&]+|) HTTP\/1\.0

	std::regex_search(request, m, e);
	auto i = m.begin() + 1;
	std::string res = *i;
	fprintf(f, "%s\n", res.c_str());
	fclose(f);
	return res;
}*/

std::string http_parse(std::string& request)
{
	char *str = new char[request.length() + 1];
	strcpy(str, request.c_str());
	char *name = strtok(str + 4, "/? ");
	std::string res = name;
	return res;	
}

void * slave_func(void *arg)
{
	std::string not_found = "HTTP/1.0 404 Not Found\r\nContent-Length:"\
			"0\r\nContent-Type: text/html\r\n\r\n";
			
	std::string result = "HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n";
	int fd = *((int *)arg);
	//std::cout << "Events = " << Events[i].events << std::endl;
	char Buffer[8152];
	int RecvResult = recv(fd, Buffer, 8152, MSG_NOSIGNAL);
	Buffer[RecvResult] = '\0';
	FILE *f = fopen("/home/box/http.log", "a");
	fprintf(f, "%s\n", Buffer);
	fclose(f);
	//std::cout << "RecvResult = " << RecvResult << std::endl;
	if ((RecvResult == 0) && (errno != EAGAIN))
	{
		shutdown(fd, SHUT_RDWR);
		close(fd);
	}
	else if (RecvResult > 0)
	{
		std::string request = Buffer;
		for (int i = 0; i < 8152; ++i)
			Buffer[i] = 0;
		std::string name = http_parse(request);
		std::string path = directory;
		path += "/" + name;
		if (FILE *file = fopen(path.c_str(), "r"))
		{
			size_t n = fread(Buffer, 1, 8152, file);
			Buffer[n] = '\0';
			result += Buffer;
			
		}
		else
		{
			result = not_found;
		}
		send(fd, result.c_str(), result.length(), MSG_NOSIGNAL);
		shutdown(fd, SHUT_RDWR);
		close(fd);
	}	
	return 0;
}

int main(int argc, char **argv)
{
    signal(SIGHUP, SIG_IGN);
    //daemon(0, 0);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	std::string host;
    std::string port;
    std::string direct;
	int c;
    static struct option long_opt[] = {
            {"host", required_argument, 0, 'h'},
            {"port", required_argument, 0, 'p'},
			{"directory", no_argument, 0, 'd'},
            {0,0,0,0}
    };
    int optIdx;
    while (1)
    {
		c = getopt_long(argc, argv, "h:p:d:", long_opt, &optIdx);
		switch(c)
		{
			case 'h':
			{
				host = optarg;
				break;
			}
			case 'p':
			{
				port = optarg;
				break;
			}
			case 'd':
			{
				direct = optarg;
				break;
			}
			default:
				break;
		}
        if(c == -1)
	    break;
    }
	directory = new char[direct.length() + 1];
    direct.copy(directory, direct.length(), 0);
    directory[direct.length()] = '\0';
	int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	struct sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(atoi(port.c_str()));
    inet_aton(host.c_str(), &SockAddr.sin_addr);
	bind(MasterSocket, (struct sockaddr *)(&SockAddr), sizeof(SockAddr));

	set_nonblock(MasterSocket);

	listen(MasterSocket, SOMAXCONN);

	int EPoll = epoll_create1(0);
	
	struct epoll_event Event;
	Event.data.fd = MasterSocket;
	Event.events = EPOLLIN;
	epoll_ctl(EPoll, EPOLL_CTL_ADD, MasterSocket, &Event);

	while(true)
	{

		int N = epoll_wait(EPoll, Events, MAX_EVENTS, -1); //-1 - вечное ожидание
		//std::cout << N << std::endl;
	
		for(int i = 0; i < N; ++i)
		{
			if(Events[i].data.fd == MasterSocket)
			{
				//std::cout << "Connect to server" << std::endl;
				int SlaveSocket = accept(MasterSocket, 0, 0);
				set_nonblock(SlaveSocket);
				Event.data.fd = SlaveSocket;
				Event.events = EPOLLIN;
				epoll_ctl(EPoll, EPOLL_CTL_ADD, SlaveSocket, &Event);
			}
			else
			{
				pthread_t thread;
				pthread_create(&thread, &attr, slave_func, &Events[i].data.fd);
			}
		}
	//	std::cout << "While cycle" << std::endl;
	}
	return 0;
}
