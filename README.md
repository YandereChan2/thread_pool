# thread_pool
简易线程池
该线程池使用```std::mutex```和```std::condition_variable```作为同步设施。
使用时，主要通过模板来指定线程池的各项参数，例如```Yc::thread_pool<5,10>```表示核心线程数为5个，最大线程数为10个的线程池，另外可以使用```Yc::thread_pool<Yc::dynamic_size,Yc::dynamic_size>```来指定可以运行期才知道其参数的线程池。
