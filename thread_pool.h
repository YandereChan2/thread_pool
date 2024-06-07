#pragma once
#include<thread>
#include<mutex>
#include<condition_variable>
#include<atomic>
#include<memory>
#include<chrono>
#include<array>
#include<exception>
#include<utility>
#include<queue>
#include<vector>
#include<type_traits>
namespace Yc
{
	constexpr size_t dynamic_size = -1;
	constexpr size_t no_limit = -1;
	template<class T, size_t N>
	class short_queue
	{
		union Ty
		{
			unsigned char c;
			T t;
		};
		std::array<Ty, N>inner;
		size_t head = 0;
		size_t tail = 0;
	public:
		constexpr bool filled()const noexcept
		{
			return (tail + 1) % N == head;
		}
		constexpr bool empty()const noexcept
		{
			return tail == head;
		}
		constexpr short_queue() = default;
		constexpr void push(const T& t)noexcept(noexcept(new(&(inner[tail]))T(t)))
		{
			new(&(inner[tail]))T(t);
			tail++;
			tail %= N;
		}
		constexpr void push(T&& t)noexcept(noexcept(new(&(inner[tail]))T(std::move(t))))
		{
			new(&(inner[tail]))T(std::move(t));
			tail++;
			tail %= N;
		}
		template<class... Args>constexpr void emplace(Args&&... args)noexcept(noexcept(new(&(inner[tail]))T(std::forward<Args>(args)...)))
		{
			new(&(inner[tail]))T(std::forward<Args>(args)...);
			tail++;
			tail %= N;
		}
		constexpr void pop()noexcept
		{
			std::destroy_at<T>(&(inner[head]));
			head++;
			head %= N;
		}
		constexpr T& front()noexcept
		{
			return inner[head].t;
		}
		constexpr const T& front()const noexcept
		{
			return inner[head].t;
		}
		constexpr T& back()noexcept
		{
			return inner[(tail + N - 1) % N].t;
		}
		constexpr const T& back()const noexcept
		{
			return inner[(tail + N - 1) % N].t;
		}
		constexpr void clear()noexcept
		{
			while (!empty())
			{
				pop();
			}
		}
		~short_queue()
		{
			clear();
		}
	};
	template<class T>
	class short_queue<T, 0>
	{
	public:
		constexpr bool filled()const noexcept
		{
			return true;
		}
		constexpr bool empty()const noexcept
		{
			return true;
		}
		void push(auto) = delete;
		void emplace(auto...) = delete;
		void pop() = delete;
		T& front() = delete;
		T& back() = delete;
		constexpr void clear() {}
	};
	template<size_t task_queue_max_size>
	struct task_queue
	{
		short_queue<std::pair<void(*)(void*), void*>, task_queue_max_size>tasks;
	};
	template<>
	struct task_queue<no_limit>
	{
		std::queue<std::pair<void(*)(void*), void*>>tasks;
	};
	template<size_t core_size>
	struct thread_pool_core
	{
		std::thread threads[core_size];
	};
	template<size_t extra_size>
	struct thread_pool_extra
	{
		std::thread extra_threads[extra_size];
		std::atomic_flag stop_flag[extra_size];
	};
	template<>
	struct thread_pool_core<dynamic_size>
	{
		std::vector<std::thread>threads;
	};
	template<>
	struct thread_pool_extra<0>
	{
	};
	template<>
	struct thread_pool_extra<dynamic_size>
	{
		std::vector<std::pair<std::thread, std::atomic_flag>> extra_threads;
	};
	enum busy_handler
	{
		joining, throw_excpetion, do_nothing
	};


	struct runtime_tuple_base
	{
		virtual ~runtime_tuple_base() = 0;
	};
	runtime_tuple_base::~runtime_tuple_base() = default;
	template<class...Args>
	struct runtime_tuple :public runtime_tuple_base
	{
		std::tuple<Args...>tup;
		template<class...Argss>
		runtime_tuple(Argss&&... args) :tup{ std::forward<Argss>(args)... }
		{
		}
		~runtime_tuple() = default;
	};
	class thread_pool_busy :public std::exception
	{
		const char* what()const noexcept override
		{
			return "线程池繁忙";
		}
	};
	struct null_type
	{
		void operator++() {}
		void operator++(int) {}
		void operator--() {}
		void operator--(int) {}
	};
	template<size_t core_size, size_t max_size = core_size, busy_handler handler = joining, unsigned long long time_out = 10000, size_t task_queue_max_size = no_limit>
	class thread_pool
	{
		static_assert(core_size > 0);
		static_assert(max_size != dynamic_size);
		static inline constexpr size_t extra_size = max_size - core_size;
		task_queue<task_queue_max_size> tasks;
		thread_pool_core<core_size>core_pool;
		thread_pool_extra<extra_size>extra_pool;
		std::mutex queue_lock;
		std::condition_variable cv;
		using counter_t = std::conditional_t<extra_size == 0, null_type, std::atomic<size_t>>;
		counter_t counter;
		std::atomic_flag stop_flag;
		template<class...Args>
		using tup_t = std::conditional_t<handler == joining, std::tuple<Args...>, runtime_tuple<Args...>>;
		template<class Fn, class...Args>
		static void invoke_function_core(void* args)
		{
			std::unique_ptr<tup_t<std::decay_t<Fn>, std::decay_t<Args>...>>source_ptr{ (tup_t<std::decay_t<Fn>, std::decay_t<Args>...>*)  args };
			[&] <size_t...INDEX>(std::index_sequence<INDEX...>)
			{
				if constexpr (handler == joining)
				{
					std::invoke(std::get<0>(*source_ptr), std::get<1 + INDEX>(*source_ptr)...);
				}
				else
				{
					std::invoke(std::get<0>(source_ptr->tup), std::get<1 + INDEX>(source_ptr->tup)...);
				}
			}(std::make_index_sequence<sizeof...(Args)>());
		}
		void try_add_thread()
		{
			if constexpr (extra_size > 0)
			{
				for (size_t i = 0; i < extra_size; i++)
				{
					if (!extra_pool.stop_flag[i].test())
					{
						extra_pool.stop_flag[i].test_and_set();
						if (extra_pool.extra_threads[i].joinable())
						{
							extra_pool.extra_threads[i].join();
						}
						extra_pool.extra_threads[i] = std::thread
						{
							[&,i]()
							{
								bool flag = false;
								std::unique_lock<std::mutex>lck(queue_lock,std::defer_lock);
								do
								{
									if (stop_flag.test())
									{
										return;
									}
									std::pair<void(*)(void*), void*>tmp;
									{
										if (flag)
										{
											lck.unlock();
										}
										std::lock_guard l{lck};
										if (!tasks.tasks.empty())
										{
											tmp = tasks.tasks.front();
											tasks.tasks.pop();
											flag = !tasks.tasks.empty();
										}
										else
										{
											goto label;
										}
									}
									tmp.first(tmp.second);
									label:
									lck.lock();
								} while (flag || (cv.wait_for(lck, std::chrono::milliseconds{ time_out }) == std::cv_status::no_timeout));
								extra_pool.stop_flag[i].clear();
							}
						};
						break;
					}
				}
			}
		}
	public:
		template<std::enable_if_t<core_size != dynamic_size, size_t> = 0>
		thread_pool()
		{
			stop_flag.clear();
			if constexpr (extra_size > 0 && extra_size != dynamic_size)
			{
				for (auto& f : extra_pool.stop_flag)
				{
					f.clear();
				}
			}
			for (auto& th : core_pool.threads)
			{
				th = std::thread
				{
					[&]()
					{
						while (!stop_flag.test())
						{
							std::pair<void(*)(void*), void*>tmp;
							{
								std::unique_lock<std::mutex>lck{ queue_lock };
								if (tasks.tasks.empty())
								{
									cv.wait(lck, [&]() {return !tasks.tasks.empty() || stop_flag.test(); });
								}
								if (tasks.tasks.empty())
								{
									return;
								}
								tmp = tasks.tasks.front();
								tasks.tasks.pop();
							}
							counter++;
							tmp.first(tmp.second);
							counter--;
						}
					}
				};
			}
		}

		template<class Fn, class...Args>
		void add_task(Fn&& fn, Args&&...args)
		{
			{
				std::lock_guard lck{ queue_lock };
				if constexpr (task_queue_max_size != no_limit)
				{
					if (tasks.tasks.filled())
					{
						if constexpr (handler == joining)
						{
							goto label;
						}
						if constexpr (handler == throw_excpetion)
						{
							throw thread_pool_busy{};
						}
						if constexpr (handler == do_nothing)
						{
							auto [_, args] = tasks.tasks.front();
							delete (runtime_tuple_base*)args;
							tasks.tasks.pop();
						}
					}
				}
				tasks.tasks.emplace(&invoke_function_core<Fn, Args...>, new tup_t<std::decay_t<Fn>, std::decay_t<Args>...>(std::forward<Fn>(fn), std::forward<Args>(args)...));
				if constexpr (extra_size > 0 && extra_size != dynamic_size)
				{
					try_add_thread();
				}
			}
			cv.notify_one();
			return;
		label:
			if constexpr (handler == joining)
			{
				std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
			}
		}
		~thread_pool()
		{
			{
				std::lock_guard lck{ queue_lock };
				while (!tasks.tasks.empty())
				{
					auto [fn, args] = tasks.tasks.front();
					tasks.tasks.pop();
					queue_lock.unlock();
					fn(args);
					queue_lock.lock();
				}
				stop_flag.test_and_set();
			}
			cv.notify_all();
			for (auto& th : core_pool.threads)
			{
				th.join();
			}
			if constexpr (extra_size > 0 && extra_size != dynamic_size)
			{
				for (auto& th : extra_pool.extra_threads)
				{
					if (th.joinable())
					{
						th.join();
					}
				}
			}
		}
	};
	template<size_t max_size, busy_handler handler, unsigned long long time_out, size_t task_queue_max_size>
	class thread_pool<dynamic_size, max_size, handler, time_out, task_queue_max_size>
	{
		static_assert(max_size == dynamic_size);
		task_queue<task_queue_max_size> tasks;
		std::vector<std::thread>core_pool;
		std::vector<std::pair<std::thread, std::atomic_flag>>extra_pool;
		std::mutex queue_lock{};
		std::condition_variable cv;
		std::optional<std::atomic<size_t>>counter;
		std::atomic_flag stop_flag{};
		template<class...Args>
		using tup_t = std::conditional_t<handler == joining, std::tuple<Args...>, runtime_tuple<Args...>>;
		size_t extra_size()const
		{
			return extra_pool.size();
		}
		template<class Fn, class...Args>
		static void invoke_function_core(void* args)
		{
			std::unique_ptr<tup_t<std::decay_t<Fn>, std::decay_t<Args>...>>source_ptr{ (tup_t<std::decay_t<Fn>, std::decay_t<Args>...>*)  args };
			[&] <size_t...INDEX>(std::index_sequence<INDEX...>)
			{
				if constexpr (handler == joining)
				{
					std::invoke(std::get<0>(*source_ptr), std::get<1 + INDEX>(*source_ptr)...);
				}
				else
				{
					std::invoke(std::get<0>(source_ptr->tup), std::get<1 + INDEX>(source_ptr->tup)...);
				}
			}(std::make_index_sequence<sizeof...(Args)>());
		}
		void try_add_thread()
		{
			if (extra_size() > 0)
			{
				for (size_t i = 0; i < extra_size(); i++)
				{
					if (!extra_pool[i].second.test())
					{
						extra_pool[i].second.test_and_set();
						if (extra_pool[i].first.joinable())
						{
							extra_pool[i].first.join();
						}
						extra_pool[i].first = std::thread
						{
							[&,i]()
							{
								bool flag = false;
								std::unique_lock<std::mutex>lck(queue_lock,std::defer_lock);
								do
								{
									if (stop_flag.test())
									{
										return;
									}
									std::pair<void(*)(void*), void*>tmp;
									{
										if (flag)
										{
											lck.unlock();
										}
										std::lock_guard l{lck};
										if (!tasks.tasks.empty())
										{
											tmp = tasks.tasks.front();
											tasks.tasks.pop();
											flag = !tasks.tasks.empty();
										}
										else
										{
											goto label;
										}
									}
									tmp.first(tmp.second);
									label:
									lck.lock();
								} while (flag || (cv.wait_for(lck, std::chrono::milliseconds{ time_out }) == std::cv_status::no_timeout));
								extra_pool[i].second.clear();
							}
						};
						break;
					}
				}
			}
		}
	public:
		thread_pool(size_t core_size, size_t max_size) :core_pool{ core_size }, extra_pool{ max_size - core_size }
		{
			if (max_size - core_size == 0)
			{
				counter = std::nullopt;
			}
			else
			{
				counter = 0;
			}
			for (auto& th : core_pool)
			{
				th = std::thread
				{
					[&]()
					{
						while (!stop_flag.test())
						{
							std::pair<void(*)(void*), void*>tmp;
							{
								std::unique_lock<std::mutex>lck{ queue_lock };
								if (tasks.tasks.empty())
								{
									cv.wait(lck, [&]() {return !tasks.tasks.empty() || stop_flag.test(); });
								}
								if (tasks.tasks.empty())
								{
									return;
								}
								tmp = tasks.tasks.front();
								tasks.tasks.pop();
							}
							if (counter)
							{
								(*counter)++;
							}
							tmp.first(tmp.second);
							if (counter)
							{
								(*counter)--;
							}
						}
					}
				};
			}
		}
		template<class Fn, class...Args>
		void add_task(Fn&& fn, Args&&...args)
		{
			{
				std::lock_guard lck{ queue_lock };
				if constexpr (task_queue_max_size != no_limit)
				{
					if (tasks.tasks.filled())
					{
						if constexpr (handler == joining)
						{
							goto label;
						}
						if constexpr (handler == throw_excpetion)
						{
							throw thread_pool_busy{};
						}
						if constexpr (handler == do_nothing)
						{
							auto [_, args] = tasks.tasks.front();
							delete (runtime_tuple_base*)args;
							tasks.tasks.pop();
						}
					}
				}
				tasks.tasks.emplace(&invoke_function_core<Fn, Args...>, new tup_t<std::decay_t<Fn>, std::decay_t<Args>...>(std::forward<Fn>(fn), std::forward<Args>(args)...));
				if (extra_size() > 0)
				{
					try_add_thread();
				}
			}
			cv.notify_one();
			return;
		label:
			if constexpr (handler == joining)
			{
				std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
			}
		}
		~thread_pool()
		{
			{
				std::lock_guard lck{ queue_lock };
				while (!tasks.tasks.empty())
				{
					auto [fn, args] = tasks.tasks.front();
					tasks.tasks.pop();
					queue_lock.unlock();
					fn(args);
					queue_lock.lock();
				}
				stop_flag.test_and_set();
			}
			cv.notify_all();
			for (auto& th : core_pool)
			{
				th.join();
			}
			if (extra_size() > 0)
			{
				for (auto& th : extra_pool)
				{
					if (th.first.joinable())
					{
						th.first.join();
					}
				}
			}
		}
	};
}
