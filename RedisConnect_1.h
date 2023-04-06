#ifndef REDIS_CONNECT_H
#define REDIS_CONNECT_H
///////////////////////////////////////////////////////////////
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/statfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/syscall.h>

#include "ResPool.h"
#define ioctlsocket ioctl
#define INVALID_SOCKET (SOCKET)(-1)

using namespace std;

class RedisConnect{
    friend class Command;

public:
    static const int OK = 1;   // 正常
	static const int FAIL = -1;  // 失败
	static const int IOERR = -2;  // IO出错
	static const int SYSERR = -3;  // 系统错误
	static const int NETERR = -4;  // 网络错误
	static const int TIMEOUT = -5;  // 超时
	static const int DATAERR = -6;  // 数据错误
	static const int SYSBUSY = -7;  // 系统繁忙
	static const int PARAMERR = -8;  // 参数错误
	static const int NOTFOUND = -9;  // 未找到
	static const int NETCLOSE = -10;  // 网络关闭
	static const int NETDELAY = -11;  // 网络延迟
	static const int AUTHFAIL = -12;  // 密码不对

public:
    static int POOL_MAXLEN;
    static int SOCKET_TIMEOUT;

public:
    class Socket{
    protected:
        int sock = INVALID_SOCKET;
    public:
        static bool IsSocketTimeout(){
            return errno == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        
        static void SocketClose(int sock){
            if(IsSocketClosed(sock)){
                return;
            }
            ::close(sock);
        }

        static bool IsSocketClosed(int sock){
            return sock < 0;
        }

        static bool SocketSetSendTimeout(int sock, int timeout){
            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = timeout % 1000 * 1000;
            return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)(&tv), sizeof(tv)) == 0;
        }

        static bool SocketSetRecvTimeout(SOCKET sock, int timeout){
            struct timeval tv;

			tv.tv_sec = timeout / 1000;
			tv.tv_usec = timeout % 1000 * 1000;

			return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)(&tv), sizeof(tv)) == 0;
        }

        int SocketConnectTimeout(const char* ip, int port, int timeout){
            u_long mode = 1;
            struct sockaddr_in addr;
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if(sock < 0){
                return INVALID_SOCKET;
            }

            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            // addr.sin_addr.s_addr = inet_addr(ip);
            inet_pton(AF_INET, ip, &addr.sin_addr);

            // 设置套接字非阻塞，mode = 0表示清除，mode非0表示设置本套接口的非阻塞标志
            ioctl(sock, FIONBIO, &mode);
            mode = 0;

            // 成功连接
            if(::connect(sock, (struct sockaddr*)(&addr), sizeof(addr)) == 0){
                ioctl(sock, FIONBIO, &mode);
                return sock;
            }

            struct epoll_event ev;
            struct epoll_event evs;
            int epoll_fd = epoll_create(5);

            if(epoll_fd < 0){
                SocketClose(sock);
                return INVALID_SOCKET;
            }

            memset(&ev, 0, sizeof(ev));

            ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;

            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev);

            if(epoll_wait(epoll_fd, &evs, 1, timeout) > 0){
                if(evs.events & EPOLLOUT){
                    int res = FAIL;
                    socklen_t len = sizeof(res);
                    // 获取sock的错误
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)(&res), &len);
                    ioctl(sock, FIONBIO, &mode);
                    // 如果没有错误
                    if(res == 0){
                        ::close(epoll_fd);
                        return sock;
                    }
                }
            }

            ::close(epoll_fd);
            SocketClose(sock);
            return INVALID_SOCKET;
        }

    public:
        void close(){
            SocketClose(sock);
            sock = INVALID_SOCKET;
        }

        bool isClosed() const{
            return IsSocketClosed(sock);
        }

        // 为sock设置发送超时时间
		bool setSendTimeout(int timeout){
			return SocketSetSendTimeout(sock, timeout);
		}

		// 为sock设置接受超时时间
		bool setRecvTimeout(int timeout){
			return SocketSetRecvTimeout(sock, timeout);
		}

        bool connect(const string& ip, int port, int timeout){
            close();
            sock = SocketConnectTimeout(ip.c_str(), port, timeout);
            return IsSocketClosed(sock) ? false : true;
        }

    public:
        int write(const void* data, int count){
            const char* str = (const char*)(data);

            int num = 0;
            int times = 0;
            int writed = 0;
            while(writed < count){
                num = send(sock, str + writed, count - writed, 0);
                if(num > 0){
                    if(num > 8){
                        times = 0;
                    }else{
                        if(++times > 100){
                            return TIMEOUT;
                        }
                    }
                    writed += num;
                }else{
                    if(IsSocketTimeout()){
                        if(++times > 100){
                            return TIMEOUT;
                        }
                        continue;;
                    }
                    return NETERR;
                }
            }
            return writed;
        }

        int read(void* data, int count, bool completed){
            char* str = (char*)(data);

            if(completed){
                int num = 0;
                int times = 0;
                int readed = 0;
                while(num < count){
                    num = recv(sock, str + readed, count - readed, 0);
                    if(num > 0){
                        if(num > 8){
                            times = 0;
                        }else{
                            if(++times > 100){
                                return TIMEOUT;
                            }
                        }
                        readed += num;
                    }else if(num == 0){
                        return NETCLOSE;
                    }else{
                        if(IsSocketTimeout()){
                            if(++times > 100){
                                return TIMEOUT;
                            }
                            continue;
                        }
                        return NETERR;
                    }
                }
                return readed;
            }else{
                int val = recv(sock, str, count, 0);
                if(val > 0){
                    return val;
                }else if(val == 0){
                    return NETCLOSE;
                }else if(IsSocketTimeout()){
                    return TIMEOUT;
                }else{
                    return NETERR;
                }
            }
        }
    };

    class Command{

    };
};

#endif