#include "RedisConnPool.h"

shared_ptr<RedisConnect> RedisConnPool::Instance() {
    return GetTemplate()->GetConn();
}

RedisConnPool* RedisConnPool::GetTemplate(){
    static RedisConnPool connPool;
    return &connPool;
}
shared_ptr<RedisConnect> RedisConnPool::GetConn() {
    shared_ptr<RedisConnect> redis = nullptr;
    if (connQue_.empty()){
        cout << "RedisConnPool busy"<<endl;
        // return nullptr;
    } 
    sem_wait(&semId_);
    {
        lock_guard<mutex> locker(mtx_);
        redis = connQue_.front();
        connQue_.pop();
    }
    return redis;
}

void RedisConnPool::FreeConn(shared_ptr<RedisConnect> redis) {
    assert(redis);
    lock_guard<mutex> locker(mtx_);
    connQue_.push(redis);
    sem_post(&semId_);
}

int RedisConnPool::GetFreeConnCount() {
    lock_guard<mutex> locker(mtx_);
    return connQue_.size();
}

void RedisConnPool::Init(const string& host, int port, const string& pwd = "",
                         int connSize = 8, int timeout = 3000, 
                         int memsz = 2 * 1024 * 1024) {
    for(int i = 0; i < connSize; ++i){
        shared_ptr<RedisConnect> redis = make_shared<RedisConnect>();
        if(redis && redis->connectRedis(host, port, timeout, memsz)){
            if(redis->auth(pwd)){
                connQue_.push(redis);
            }
        }
        if(!redis){
            assert(redis);
        }
    }
    MAX_CONN_ = connSize;
    cout << "最大连接数" << MAX_CONN_ << endl;
    sem_init(&semId_, 0, MAX_CONN_);
}

void RedisConnPool::ClosePool() {
    lock_guard<mutex> locker(mtx_);
    while(!connQue_.empty()){
        auto item = connQue_.front();
        connQue_.pop();
        // 关闭连接
    }
}

RedisConnPool::RedisConnPool() : useCount_(0), freeCount_(0) {

}

RedisConnPool::~RedisConnPool() {
    ClosePool();
}

int RedisConnect::SOCKET_TIMEOUT = 10;