#ifndef REDISCONNPOOL
#define REDISCONNPOOL
#include <ctime>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <sstream>
#include <iostream>
#include <iterator>
#include <typeinfo>
#include <algorithm>
#include <functional>

#include "typedef.h"
#include "RedisConn.h"

using namespace std;

class RedisConnPool {
public:
    static shared_ptr<RedisConnect> Instance();
    static RedisConnPool *GetTemplate();
    shared_ptr<RedisConnect> GetConn();
    void FreeConn(shared_ptr<RedisConnect> conn);
    int GetFreeConnCount();
    void Init(const string& host, int port, const string& pwd,
                         int connSize, int timeout, 
                         int memsz);
    void ClosePool();
     
private:
    RedisConnPool();
    ~RedisConnPool();

    int MAX_CONN_;   // 最大的连接数
    int useCount_;   //  当前的用户数
    int freeCount_;  //  空闲的用户数，没用上

    std::queue<shared_ptr<RedisConnect>> connQue_;
    std::mutex mtx_;
    sem_t semId_;
};

#endif

