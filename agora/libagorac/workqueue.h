#ifndef WORK_QUEUE_H_
#define WORK_QUEUE_H_

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

/*
 * A small thread-safe FIFO. get() returns nullptr when empty; waitForWork()
 * blocks until an item arrives or close() is called.
 */
template <class T>
class WorkQueue{
public:

	void add(T work){
	   {
	      std::lock_guard<std::mutex> guard(_qMutex);
	      _workQueue.push(std::move(work));
	   }
	   _condition.notify_one();
	}

	//get the next available work on the queue (nullptr when empty)
	T get(){

		std::lock_guard<std::mutex> guard(_qMutex);

		if(_workQueue.empty()) return nullptr;

		T newWork=_workQueue.front();
		_workQueue.pop();

		return newWork;
	}

	bool isEmpty(){
		std::lock_guard<std::mutex> guard(_qMutex);
		return _workQueue.empty();
	}

	//block until work is available or the queue is closed
	void waitForWork(){

	     std::unique_lock<std::mutex> lk(_qMutex);
	     _condition.wait(lk, [this]{return _closed || !_workQueue.empty();});
	}

	size_t size(){
		std::lock_guard<std::mutex> guard(_qMutex);
		return _workQueue.size();
	}

	//wake up any waiters permanently (the queue stays usable for get())
	void close(){
	   {
	      std::lock_guard<std::mutex> guard(_qMutex);
	      _closed=true;
	   }
	   _condition.notify_all();
	}

	void clear(){
	    std::lock_guard<std::mutex> guard(_qMutex);
	    while(!_workQueue.empty()){
	        _workQueue.pop();
	    }
	}

private:
	std::queue<T>                _workQueue;
	std::condition_variable      _condition;
	std::mutex                   _qMutex;
	bool                         _closed=false;
};

class Work;
using Work_ptr=std::shared_ptr<Work>;
typedef std::shared_ptr<WorkQueue <Work_ptr> >       WorkQueue_ptr;


#endif  // WORK_QUEUE_H_
