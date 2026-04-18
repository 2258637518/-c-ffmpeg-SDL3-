#pragma once
#include <functional>
#include<queue>
#include<mutex>
#include<condition_variable>
extern "C"
{
#include <libavutil/mem.h>
}
//使用模板应对不同但相似的ffmpeg数据结构
template <typename T>
class ThreadQueue
{
private:
    std::queue<T>q;
    std::mutex mtx;
    std::condition_variable cond;
    bool abort_request = false;

public:
    void abort()//打断线程的休眠
    {
        std::lock_guard<std::mutex> lock(mtx);
        abort_request = true;
        cond.notify_all();
    }
    void push(T t)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if(abort_request) return;
        q.push(t);
        cond.notify_one();
    }
    T pop()
    {
        std::unique_lock<std::mutex> unique(mtx);
        cond.wait(unique,[this](){
            return !q.empty() || abort_request;
        });
        if(abort_request || q.empty())return nullptr;
        T t=q.front();
        q.pop();
        return t;
    }
    void clear()//使用av_free安全的释放队列中的数据
    {
        std::lock_guard<std::mutex> lock(mtx);
        while(!q.empty())
        {
            T t=q.front();
            av_free(&t);
            q.pop();
        }
    }
    void flush(std::function<void(T&)> deleter)//使用回调函数对不同的数据类型调用不同的内存释放函数
    {
        std::lock_guard<std::mutex> lock(mtx);
        while(!q.empty())
        {
            T t=q.front();
            if(deleter)deleter(t);
            q.pop();
        }
    }
    int size()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return q.size();
    }
};