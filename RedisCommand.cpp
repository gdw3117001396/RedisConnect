#include "RedisConnect.h"

#define ColorPrint(__COLOR__, __FMT__, ...)		\
SetConsoleTextColor(__COLOR__);					\
printf(__FMT__, __VA_ARGS__);					\
SetConsoleTextColor(eWHITE);					\

bool CheckCommand(const char* fmt, ...)
{
	va_list args;
	char buffer[64 * 1024] = {0};

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	va_end(args);

	printf("%s", buffer);

	while (true)
	{
		int n = getch();

		if (n == 'Y' || n == 'y')
		{
			SetConsoleTextColor(eGREEN);
			printf(" (YES)\n");
			SetConsoleTextColor(eWHITE);

			return true;
		}
		else if (n == 'N' || n == 'n')
		{
			SetConsoleTextColor(eRED);
			printf(" (NO)\n");
			SetConsoleTextColor(eWHITE);

			return false;
		}
	}

	return false;
}

int main(int argc, char** argv)
{
	auto GetCmdParam = [&](int idx){
		return idx < argc ? argv[idx] : NULL;
	};

	string val;
	RedisConnect redis;
	const char* ptr = NULL;  //redis
	const char* cmd = GetCmdParam(1);  // 命令
	const char* key = GetCmdParam(2);  // 键值
	//const char* field = GetCmdParam(3); // 没用到

	int port = 6379;
	// host = 127.0.0.1:6379
	const char* host = getenv("REDIS_HOST"); // 获取环境变量host
	// passwd = 123456
	const char* passwd = getenv("REDIS_PASSWORD"); // 获取环境变量密码

	if (host)
	{
		// 查找':'第一次出现的位置,ptr = :6379
		if (ptr = strchr(host, ':'))
		{
			// 127.0.0.1,相当于迭代器构造函数
			static string shost(host, ptr);
			port = atoi(ptr + 1); // 6379 端口号
			host = shost.c_str(); // 127.0.0.1 主机号
		}
	}

	if (host == NULL || *host == 0){
		host = "127.0.0.1";
	} 

	if (redis.connect(host, port))
	{
		if (passwd && *passwd)
		{
			if (redis.auth(passwd) < 0)
			{
				ColorPrint(eRED, "REDIS[%s][%d]验证失败\n", host, port);

				return -1;
			}
		}

		if (cmd == NULL)
		{
			ColorPrint(eRED, "%s\n", "请输入要执行的命令");

			return -1;
		}

		int res = 0;
		string tmp = cmd;
		// 将所有命令变为大写
		std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::toupper);

		if (tmp == "DELS" && key && *key){
			vector<string> vec;
			// 查看键值是否存在，并将对应键值存入到vec中
			if (redis.keys(vec, key) > 0){
				if (CheckCommand("确认要删除键值[%s]?", key)){
					ColorPrint(eWHITE, "%s\n", "--------------------------------------");

					for (const string& item : vec){
						if (redis.del(item)){
							ColorPrint(eGREEN, "删除键值[%s]成功\n", item.c_str());
						}
						else{
							ColorPrint(eRED, "删除键值[%s]失败\n", item.c_str());
						}
					}

					ColorPrint(eWHITE, "%s\n\n", "--------------------------------------");
				}
			}else
			{
				ColorPrint(eRED, "删除键值[%s]失败\n", key);
			}
		}else{
			int idx = 1;
			RedisConnect::Command request; // redis命令请求

			while (true)
			{
				// 从命令开始取，后面所有的参数
				const char* data = GetCmdParam(idx++);

				if (data == NULL) break;

				request.add(data);
			}

			if ((res = redis.execute(request)) >= 0)
			{
				ColorPrint(eWHITE, "执行命令[%s]成功[%d][%d]\n", cmd, res, redis.getStatus());

				const vector<string>& vec = request.getDataList();

				if (vec.size() > 0)
				{
					ColorPrint(eWHITE, "%s\n", "--------------------------------------");

					for (const string& msg : vec)
					{
						ColorPrint(eGREEN, "%s\n", msg.c_str());
					}

					ColorPrint(eWHITE, "%s\n", "--------------------------------------");
					ColorPrint(eWHITE, "共返回%ld条记录\n\n", vec.size());
				}
			}
			else
			{
				ColorPrint(eRED, "执行命令[%s]失败[%d][%s]\n", cmd, res, redis.getErrorString().c_str());
			}
		}
	}
	else
	{
		ColorPrint(eRED, "REDIS[%s][%d]连接失败\n", host, port);
	}

	return 0;
}
