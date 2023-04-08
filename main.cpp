#include "RedisConnPool.h"

int main(int argc, char** argv)
{
	string val;

	RedisConnPool::GetTemplate()->Init("127.0.0.1", 6379, "123456", 8, 3000, 2 * 1024 * 1024);
	
	Sleep(1000);
	for(size_t i = 0; i < 10; ++i){
		std::thread([&](){
			int index = i;
				shared_ptr<RedisConnect> redis = RedisConnPool::Instance();
				/*if(!redis){
					return;
				}*/
				redis->set("key" + to_string(index), "val");
				string temp = "thread" + to_string(index) + "拿到了" + redis->get("key" + to_string(index));
				puts(temp.c_str());
				Sleep(2000);
				if (redis->lock("lockey"))
				{
					// puts("获取分布式锁成功");
					cout<<"thread" + to_string(index) + "获取分布式锁成功"<<endl;
					//释放分布式锁
					Sleep(2000);
					if (redis->unlock("lockey"))
					{
						// puts("释放分布式锁成功");
						cout<<"thread" + to_string(index) + "释放分布式锁成功"<<endl;
					}
				}
				temp = "thread" + to_string(index) + "放回了连接";
				puts(temp.c_str());
				RedisConnPool::GetTemplate()->FreeConn(redis);			
		}).detach();
	}
		
	//shared_ptr<RedisConnect> redis = RedisConnPool::Instance();
	//初始化连接池
	// RedisConnect::Setup("127.0.0.1", 6379, "123456");
	
	//从连接池中获取一个连接
	// shared_ptr<RedisConnect> redis = RedisConnect::Instance();
 
	//设置一个键值
	//redis->set("key", "val");

	//获取键值内容
	//redis->get("key", val);
 
	//执行expire命令设置超时时间
	//redis->execute("expire", "key", 60);
 
	//获取超时时间(与ttl(key)方法等价)
	//redis->execute("ttl", "key");
 
	//调用getStatus方法获取ttl命令执行结果
	//printf("超时时间：%d\n", redis->getStatus());
 
	//执行del命令删除键值
	//redis->execute("del", "key");

	//获取分布式锁
	/*
	if (redis->lock("lockey"))
	{
		puts("获取分布式锁成功");

		//释放分布式锁
		if (redis->unlock("lockey"))
		{
			puts("释放分布式锁成功");
		}
	}*/
	Sleep(10000);
	puts("结束");
	return 0;
}