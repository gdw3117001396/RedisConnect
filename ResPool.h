#ifndef XG_RESPOOL_H
#define XG_RESPOOL_H
//////////////////////////////////////////////////////////////////////////////
#include "typedef.h"

#include <ctime>
#include <mutex>
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

using namespace std;

template<typename T> class ResPool
{
	class Data
	{
	public:
		int num; // 拿的次数
		time_t utime; // 时间
		shared_ptr<T> data; // 数据

		// utime修改为当前时间，然后num + 1,返回的是data指针
		shared_ptr<T> get()
		{
			utime = time(NULL);
			++num;

			return data;
		}
		// 构造函数
		Data(shared_ptr<T> data)
		{
			update(data);
		}

		// 重置数据，时间，num,data都重置
		void update(shared_ptr<T> data)
		{
			this->num = 0;
			this->data = data;
			this->utime = time(NULL);
		}
	};

protected:
	mutex mtx; // 锁
	int maxlen;  // 
	int timeout; // 过期时间
	vector<Data> vec; // 
	function<shared_ptr<T>()> func; // 函数指针
public:

	shared_ptr<T> grasp(){
		int len = 0;
			int idx = -1;
			shared_ptr<T> tmp;
			time_t now = time(NULL); // 获取当前时间

			mtx.lock();  //加锁

			len = vec.size();

			for (int i = 0; i < len; i++)
			{
				Data& item = vec[i];
				// 当前data保存保存的指针为空，或者data的引用数为1
				if (item.data.get() == NULL || item.data.use_count() == 1)
				{
					if (tmp = item.data)
					{
						// 超时了
						if (item.num < 100 && item.utime + timeout > now)
						{
							shared_ptr<T> data = item.get();

							mtx.unlock();

							return data;
						}

						item.data = NULL;
					}

					idx = i; // 当前的数据空的或者只有一个引用，并且没有超时
				}
			}

			mtx.unlock();

			if (idx < 0)
			{
				if (len >= maxlen) return shared_ptr<T>();

				shared_ptr<T> data = func();

				if (data.get() == NULL) return data;

				mtx.lock();

				if (vec.size() < maxlen) vec.push_back(data);

				mtx.unlock();

				return data;
			}

			shared_ptr<T> data = func();

			if (data.get() == NULL) return data;

			mtx.lock();

			vec[idx].update(data);

			mtx.unlock();

			return data;
	}

	shared_ptr<T> get()
	{
		if (timeout <= 0) return func();
/*
		auto grasp = [&](){
			int len = 0;
			int idx = -1;
			shared_ptr<T> tmp;
			time_t now = time(NULL); // 获取当前时间

			mtx.lock();  //加锁

			len = vec.size();

			for (int i = 0; i < len; i++)
			{
				Data& item = vec[i];
				// 当前data保存保存的指针为空，或者data的引用数为1
				if (item.data.get() == NULL || item.data.use_count() == 1)
				{
					if (tmp = item.data)
					{
						// 超时了
						if (item.num < 100 && item.utime + timeout > now)
						{
							shared_ptr<T> data = item.get();

							mtx.unlock();

							return data;
						}

						item.data = NULL;
					}

					idx = i; // 当前的数据空的或者只有一个引用，并且没有超时
				}
			}

			mtx.unlock();

			if (idx < 0)
			{
				if (len >= maxlen) return shared_ptr<T>();

				shared_ptr<T> data = func();

				if (data.get() == NULL) return data;

				mtx.lock();

				if (vec.size() < maxlen) vec.push_back(data);

				mtx.unlock();

				return data;
			}

			shared_ptr<T> data = func();

			if (data.get() == NULL) return data;

			mtx.lock();

			vec[idx].update(data);

			mtx.unlock();

			return data;
		};
*/

		shared_ptr<T> data = grasp();

		if (data) return data;

		time_t endtime = time(NULL) + 3;

		while (true)
		{
			Sleep(10);
			// 抓取一个数据不为空
			if (data = grasp()) return data;

			if (endtime < time(NULL)) break;
		}

		return data;
	}
	void clear()
	{
		lock_guard<mutex> lk(mtx);

		vec.clear();
	}

	int getLength() const
	{
		return maxlen;
	}

	int getTimeout() const
	{
		return timeout;
	}
	// 遍历数据，将第一个和传入的data一样的置空
	void disable(shared_ptr<T> data)
	{
		lock_guard<mutex> lk(mtx);

		for (Data& item : vec)
		{
			if (data == item.data)
			{
				item.data = NULL;

				break;
			}
		}
	}
	// 设置最大长度，如果数据超过了最大长度直接清空
	void setLength(int maxlen)
	{
		lock_guard<mutex> lk(mtx);

		this->maxlen = maxlen;

		if (vec.size() > maxlen) vec.clear();
	}
	// 设置超时时间
	void setTimeout(int timeout)
	{
		lock_guard<mutex> lk(mtx);

		this->timeout = timeout;

		if (timeout <= 0) vec.clear();
	}
	// 
	void setCreator(function<shared_ptr<T>()> func)
	{
		lock_guard<mutex> lk(mtx);

		this->func = func;
		this->vec.clear();
	}
	// 初始最大长度和超时时间
	ResPool(int maxlen = 8, int timeout = 60)
	{
		this->timeout = timeout;
		this->maxlen = maxlen;
	}
	// 初始最大长度和超时时间，还有函数指针
	ResPool(function<shared_ptr<T>()> func, int maxlen = 8, int timeout = 60)
	{
		this->timeout = timeout;
		this->maxlen = maxlen;
		this->func = func;
	}
};
//////////////////////////////////////////////////////////////////////////////
#endif