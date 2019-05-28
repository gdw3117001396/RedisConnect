# RedisConnect

## 介绍
##### 1、RedisConnect基于C++11实现的简单易用的Redis客户端。
##### 2、源码只包含一个头文件与一个命令行工具源文件，无需编译安装，真正做到零依赖。
##### 3、自带连接池功能，调用Setup方法初始化连接池，然后Instance方法就可以获取一个连接。
##### 4、RedisConnect包装了常用的redis命令，对于未包装的命令你可以使用可变参模板方法(execute)进行调用。

## 安装方法
##### 1、下载源码
###### git clone https://gitee.com/xungen/redisconnect.git

##### 2、直接在工程中包含RedisConnect.h头文件即可(示例代码如下)
```
#include "redis/RedisConnect.h"

int main(int argc, char** argv)
{
	string val;
	vector<string> vec;

	//初始化连接池
	RedisConnect::Setup("127.0.0.1", 4444);

	//从连接池中获取一个连接
	shared_ptr<RedisConnect> redis = RedisConnect::Instance();

	//设置一个键值
	redis->set("key", "val");
	
	//获取键值内容
	redis->get("key", val);

	//执行expire命令设置超时时间
	redis->execute("expire", "key", 60);

	//获取超时时间(与ttl(key)方法等价)
	redis->execute("ttl", "key");

	//调用getStatus方法获取ttl命令执行结果
	printf("超时时间：%d\n", redis->getStatus());

	//执行del命令删除键值
	redis->execute("del", "key");

	return 0;
}
```