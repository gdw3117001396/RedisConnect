#ifndef REDIS_CONNECT_H
#define REDIS_CONNECT_H
///////////////////////////////////////////////////////////////
#include "ResPool.h"

#ifdef XG_LINUX

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

#define ioctlsocket ioctl
#define INVALID_SOCKET (SOCKET)(-1)

typedef int SOCKET;

#endif

using namespace std;

// 主要的redis连接类
class RedisConnect
{
	typedef std::mutex Mutex;
	typedef std::lock_guard<mutex> Locker;

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
	static int POOL_MAXLEN;  // 最大池子个数
	static int SOCKET_TIMEOUT;  // sokect超时

public:
	class Socket{
	protected:
		SOCKET sock = INVALID_SOCKET;

	public:
		// 超时
		static bool IsSocketTimeout(){
            return errno == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }

        // 关闭socket
        static void SocketClose(int sock){
            if(IsSocketClosed(sock)){
                return;
            }
            ::close(sock);
        }
		
		// socket是否关闭了
		 static bool IsSocketClosed(int sock){
            return sock < 0;
        }

		// 为socket设置发送超时时间
		static bool SocketSetSendTimeout(int sock, int timeout){
            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = timeout % 1000 * 1000;
            return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)(&tv), sizeof(tv)) == 0;
        }

		// 为socket设置接收超时时间
		static bool SocketSetRecvTimeout(SOCKET sock, int timeout){
            struct timeval tv;

			tv.tv_sec = timeout / 1000;
			tv.tv_usec = timeout % 1000 * 1000;

			return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)(&tv), sizeof(tv)) == 0;
        }

		// socket连接服务器
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
		// 关闭sock
		void close(){
            SocketClose(sock);
            sock = INVALID_SOCKET;
        }

		// sock是否关闭
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

		// 连接
		bool connect(const string& ip, int port, int timeout){
            close();
            sock = SocketConnectTimeout(ip.c_str(), port, timeout);
            return IsSocketClosed(sock) ? false : true;
        }

	public:
		// 发送消息
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
		
		// 接受消息
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
		friend RedisConnect;

	protected:
		int status;   // 状态
		string msg;   // 提示信息
		vector<string> res;  // 收到的回复字段
		vector<string> vec;  // 所有的命令字段

	protected:
		// 解析返回消息,len表示msg的长度
        int parse(const char* msg, int len){
            /*
                *2
                $4
                keys
                $1
                *
            */
            /*
				简单字符串：Simple Strings，第一个字节响应 +  "+OK\r\n"
				错误：Errors，第一个字节响应 -  "-Error message\r\n"
				整型：Integers，第一个字节响应 :   :0\r\n 和 :1000\r\n
				批量字符串：Bulk Strings，第一个字节响应 $  
                    批量回复，是一个大小在 512 Mb 的二进制安全字符串  
                    "$5\r\nhello\r\n" 
				数组：Arrays，第一个字节响应 *
                "*2\r\n
                  $5\r\n
                  hello\r\n
                  $5\r\n
                  world\r\n"
			*/
            if (*msg == '$'){
                const char* end = parseNode(msg, len);
                if(end == NULL){
                    return DATAERR;
                }
                switch (end - msg){
                    case 0:
                        return TIMEOUT;
                    case 1:
                        return NOTFOUND;
                }
                return OK;
            }

            const char* str = msg + 1;
            const char* end = strstr(str, "\r\n");

            if(end == NULL){
                return TIMEOUT;
            }

            if(*msg == '+' || *msg == '-' || *msg == ':'){
                this->status = OK;
                this->msg = string(str, end);
                if(*msg == '+'){
                    return OK;
                }
                if(*msg == '-'){
                    return FAIL;
                }
                // 如果是整型,如果是整型status就是数字
                this->status = atoi(str);
                return OK;
            }
            /*
                "*2\r\n
                  $5\r\n
                  hello\r\n
                  $5\r\n
                  world\r\n"
            */
            if(*msg == '*'){
                int cnt = atoi(str);
                const char* tail = msg + len;
                vec.clear();
                str = end + 2;
                while(cnt-- > 0){
                    if(*str == '*'){
                        return parse(str, tail - str);
                    }
                    end = parseNode(str, tail - str);

                    if(end == NULL){
                        return DATAERR;
                    }
                    if(end == str){
                        return TIMEOUT;
                    }

                    str = end;
                }
                return res.size();
            }
        }

        const char* parseNode(const char* msg, int len){
            const char* str = msg + 1;
            // 返回str种第一次出现"\r\n"的位置,如果不存在就直接返回msg
            const char* end = strstr(str, "\r\n");
            if(end == NULL){
                return msg;
            }
            // "$5\r\nhello\r\n"  sz = 5
            int sz = atoi(str);
            if(sz < 0){
                return msg + sz;
            }
            // 跳过\r\n
            str = end + 2;
            end = str + sz + 2;
            if(msg + len < end){
                return msg;
            }
            res.push_back(string(str, str + sz));
            return end;
        }

	public:
		Command(): status(0){}
        Command(const string& cmd): status(0){
            vec.push_back(cmd);
        }

        void add(const char* val){
            vec.push_back(val);
        }

        void add(const string& val){
            vec.push_back(val);
        }
        
        template<typename T>
        void add(T val){
            add(to_string(val));
        }

        template<typename T, typename ...ARGS>
        void add(T val, ARGS ...args){
            add(val);
            add(args...);
        }

	public:
		// 将vec用特定的方式拼接成string
        string toString() const{
			// set k1 v1 
			/*  resp协议
				*3\r\n
					$3\r\nSET\r\n
					$2\r\nk1\r\n
					$2\r\nv2\r\n
			*/
            string out;
            out += "*" + to_string(vec.size()) + "\r\n";
            for(const string& item : vec){
                out += "$" + to_string(item.size()) + + "\r\n" + item +"\r\n";
            }
            return out;
        }

		// 获取指定索引的结果
        string get(int idx) const{
            return res.at(idx);
        }

        // 获得整个结果集
        const vector<string>& getDataList() const{
            return res;
        }

		int getResult(RedisConnect* redis, int timeout)
		{
			// 发送消息，再接收消息
			auto doWork = [&](){
                string msg = toString();
                cout<< msg << endl;
                Socket& sock = redis->sock;
                if(sock.write(msg.c_str(), msg.size()) < 0){
                    return NETERR;
                }

                int len = 0;
                int delay = 0;
                int readed = 0;
                char* dest = redis->buffer;
                const int maxsz = redis->memsz;

                while(readed < maxsz){
                    if((len = sock.read(dest + readed, maxsz - readed, false)) < 0){
                        return len;
                    }else if(len == 0){
                        delay += SOCKET_TIMEOUT;
                        if(delay > timeout){
                            return TIMEOUT;
                        }
                    }else{
                        dest[readed += len] = 0;
                        if((len = parse(dest, readed)) == TIMEOUT){
                            delay = 0;
                        }else{
                            return len;
                        }
                    }
                }
                return PARAMERR;
            };

			status = 0;
            msg.clear();
            redis->code = doWork();
            
            if(redis->code < 0 && msg.empty()){
                switch (redis->code)
				{
				case SYSERR:
					msg = "system error";
					break;
				case NETERR:
					msg = "network error";
					break;
				case DATAERR:
					msg = "protocol error";
					break;
				case TIMEOUT:
					msg = "response timeout";
					break;
				case NOTFOUND:
					msg = "element not found";
					break;
				default:
					msg = "unknown error";
					break;
				}
            }
            redis->status = status;
            redis->msg = msg;
            return redis->code;
		}
	};

protected:
	int code = 0;  // redis当前状态，1是正常，其它都是错误
	int port = 0;  // 端口号
	int memsz = 0; // 缓冲区大小
	int status = 0;   // 
	int timeout = 0;  // 超时时间
	char* buffer = NULL; // 缓冲区

	string msg;   // 提示信息
	string host;  // 主机ip地址
	Socket sock;  // fd
	string passwd;  // 密码

public:
	~RedisConnect()
	{
		close();
	}

public:
	// 获取状态
	int getStatus() const{
		return status;
	}
    
    // 返回错误码，redis连接的错误,无错误返回0
    int getErrorCode() const{
        if(sock.isClosed()){
            return FAIL;
        }
        return code < 0 ? code : 0;
    }

    // 返回错误信息
    string getErrorString() const{
        return msg;
    }

public:
	void close(){
        if(buffer){
            delete[] buffer;
            buffer = NULL;
        }
        sock.close();
    }

    bool reconnect(){
        if(host.empty()){
            return false;
        }

        return connect(host, port, timeout, memsz) && auth(passwd) > 0;
    }

    int execute(Command& cmd){
        return cmd.getResult(this, timeout);
    }

	//调用成功返回值不小于零(你可以马上调用getStatus方法获取redis返回结果)
	template<typename T, typename ...ARGS>
    int execute(T val, ARGS ...args){
        Command cmd;
        cmd.add(val, args...);
        return execute(cmd);
    }

	//调用成功返回值不小于零(redis返回内容保存在vec数组中)
	template<typename T, typename ...ARGS>
    int execute(vector<string>& vec, T val, ARGS ...args){
        Command cmd;
        cmd.add(val, args...);

        execute(cmd);

        if(code > 0){
            swap(vec, cmd.res);
        }
        return code;
    }

	// 初始化连接
	bool connect(const string& host, int port, int timeout = 3000, int memsz = 2 * 1024 * 1024){
        close();
        if(sock.connect(host, port, timeout)){
            sock.setSendTimeout(SOCKET_TIMEOUT);
			sock.setRecvTimeout(SOCKET_TIMEOUT);

			this->host = host;
			this->port = port;
			this->memsz = memsz;
			this->timeout = timeout;
			this->buffer = new char[memsz + 1];
        }
        return buffer ? true : false;
    }

public:
	int ping(){
        return execute("ping");
    }

    int del(const string& key){
        return execute("del", key);
    }

    int ttl(const string& key){
        return execute("ttl", key) == OK ? status : code;
    }    

    int hlen(const string& key){
        return execute("hlen", key) == OK ? status : code;
    }

    int auth(const string& passwd){
        this->passwd = passwd;
        if(passwd.empty()){
            return OK;
        }
        return execute("auth", passwd);
    }

    int get(const string& key, string& val){
        vector<string> vec;
        if(execute(vec, "get", key) <= 0){
            return code;
        }

        val = vec[0];
        return code;
    }

    int decr(const string& key, int val = 1){
		return execute("decrby", key, val);
	}

	int incr(const string& key, int val = 1){
		return execute("incrby", key, val);
	}

	int expire(const string& key, int timeout){
		return execute("expire", key, timeout);
	}

    // 查看键值是否存在
	int keys(vector<string>& vec, const string& key){
		return execute(vec, "keys", key);
	}

	int hdel(const string& key, const string& filed){
		return execute("hdel", key, filed);
	}

    int hget(const string& key, const string& filed, string& val){
		vector<string> vec;

        if(execute(vec, "hget", key, filed) <= 0){
            return code;
        }    

        val = vec[0];
        return code;
    }

    int set(const string& key, const string& val, int timeout = 0){
		return timeout > 0 ? execute("setex", key, timeout, val) : execute("set", key, val);
	}

    int hset(const string& key, const string& filed, const string& val){
		return execute("hset", key, filed, val);
	}

public:
    int pop(const string& key, string& val){
		return lpop(key, val);
	}

	int lpop(const string& key, string& val){
		vector<string> vec;

		if (execute(vec, "lpop", key) <= 0) {
            return code;
        }

		val = vec[0];

		return code;
	}

	int rpop(const string& key, string& val){
		vector<string> vec;

		if (execute(vec, "rpop", key) <= 0) {
            return code;
        }

		val = vec[0];

		return code;
	}

	int lpush(const string& key, const string& val){
		return execute("lpush", key, val);
	}

	int rpush(const string& key, const string& val){
		return execute("rpush", key, val);
	}

	int lrange(vector<string>& vec, const string& key, int start, int end){
		return execute(vec, "lrange", key, start, end);
	}

public:
	int zrem(const string& key, const string& filed){
		return execute("zrem", key, filed);
	}

	int zadd(const string& key, const string& filed, int score){
		return execute("zadd", key, score, filed);
	}

	int zrange(vector<string>& vec, const string& key, int start, int end, bool withscore = false){
		return withscore ? execute(vec, "zrange", key, start, end, "withscores") : execute(vec, "zrange", key, start, end);
	}

public:
    template<typename ...ARGS>
	int eval(const string& lua){
		vector<string> vec;

		return eval(lua, vec);
	}

	template<typename ...ARGS>
	int eval(const string& lua, const string& key, ARGS ...args)
	{
		vector<string> vec;
	
		vec.push_back(key);
	
		return eval(lua, vec, args...);
	}
	
	template<typename ...ARGS>
	int eval(const string& lua, const vector<string>& keys, ARGS ...args)
	{
		vector<string> vec;

		return eval(vec, lua, keys, args...);
	}

	template<typename ...ARGS>
	int eval(vector<string>& vec, const string& lua, const vector<string>& keys, ARGS ...args)
	{
		int len = 0;
		Command cmd("eval");

		cmd.add(lua);
		cmd.add(len = keys.size());

		if (len-- > 0)
		{
			for (int i = 0; i < len; i++) {
                cmd.add(keys[i]);
            }
			cmd.add(keys.back(), args...);
		}

		cmd.getResult(this, timeout);
	
		if (code > 0) std::swap(vec, cmd.res);

		return code;
	}

	string get(const string& key){
        string res;
        get(key, res);
        return res;
    }

    string hget(const string& key, const string& filed){
		string res;

		hget(key, filed, res);

		return res;
	}

	const char* getLockId(){
        thread_local char id[0xFF] = {0};
        
        auto GetHost = [](){
			char hostname[0xFF];

			if (gethostname(hostname, sizeof(hostname)) < 0) {
				return "unknow host";
			}
			
			struct hostent* data = gethostbyname(hostname);

			return (const char*)inet_ntoa(*(struct in_addr*)(data->h_addr_list[0]));
		};

        if(*id == 0){
            snprintf(id, sizeof(id) - 1, "%s:%ld:%ld", GetHost(), (long)getpid(), (long)syscall(SYS_gettid));
        }
        return id;
    }

    bool unlock(const string& key){
		const char* lua = "if redis.call('get',KEYS[1])==ARGV[1] then return redis.call('del',KEYS[1]) else return 0 end";

		return eval(lua, key, getLockId()) > 0 && status == OK;
	}

    bool lock(const string& key, int timeout = 30){
		int delay = timeout * 1000;

		for (int i = 0; i < delay; i += 10){
			if (execute("set", key, getLockId(), "nx", "ex", timeout) >= 0) {
                return true;
            }
			Sleep(10);
		}

		return false;
	}

protected:
	// 从连接池中拿一条连接
	virtual shared_ptr<RedisConnect> grasp() const{
		//只有第一次会初始化,就是
		static ResPool<RedisConnect> pool([&]() {
			shared_ptr<RedisConnect> redis = make_shared<RedisConnect>();

			if (redis && redis->connect(host, port, timeout, memsz)){
				if (redis->auth(passwd)) {
					return redis;
				}
			}
			return redis = NULL;
		}, POOL_MAXLEN);

		shared_ptr<RedisConnect> redis = pool.get();

		// 当前拿出来的连接有问题，再重新拿一条
		if (redis && redis->getErrorCode())
		{
			pool.disable(redis);

			return grasp();
		}

		return redis;
	}

public:
	static bool CanUse(){
		return GetTemplate()->port > 0;
	}

	static RedisConnect* GetTemplate(){
		static RedisConnect redis;
		return &redis;
	}

	static void SetMaxConnCount(int maxlen){
		if (maxlen > 0) POOL_MAXLEN = maxlen;
	}

	// 获取redis连接实例
	static shared_ptr<RedisConnect> Instance(){
		return GetTemplate()->grasp();
	}

	//  初始化连接池
	static void Setup(const string& host, int port, const string& passwd = "", int timeout = 3000, int memsz = 2 * 1024 * 1024){
		// 不处理SIGPIPE信号，因为若是尝试send到一个disconnected socket上，就会让底层抛出一个SIGPIPE信号
		signal(SIGPIPE, SIG_IGN);

		RedisConnect* redis = GetTemplate();

		redis->host = host;
		redis->port = port;
		redis->memsz = memsz;
		redis->passwd = passwd;
		redis->timeout = timeout;
	}
};

int RedisConnect::POOL_MAXLEN = 8;
int RedisConnect::SOCKET_TIMEOUT = 10;
	
///////////////////////////////////////////////////////////////
#endif