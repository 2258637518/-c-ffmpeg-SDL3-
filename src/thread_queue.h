#pragma once
#include<queue>
#include<mutex>
#include<condition_variable>
extern "C"
{
#include <libavutil/mem.h>
}
template <typename T>
class ThreadQueue
{
private:
    std::queue<T>q;
    std::mutex mtx;
    std::condition_variable cond;
    bool abort_request = false;

public:
    void abort()
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
    void clear()
    {
        std::lock_guard<std::mutex> lock(mtx);
        while(!q.empty())
        {
            T t=q.front();
            av_free(&t);
            q.pop();
        }
    }
    int size()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return q.size();
    }
};