#include <zlutils.hpp>

namespace zl {

    struct future *Future::get_c(){
        return this->_fut;
    }

    Future::Future(
        struct future *fut
    ){
        this->_fut = fut;
    }

    Future::~Future(){
        // TODO: this->_fut
    }

    struct allocator *Allocator::get_c(){
        return this->allc;
    }


    void Allocator::free(void *ptr){
        alc_free(this->allc, ptr);
    }

    Allocator::Allocator(struct allocator *allc){
        this->allc = allc;
    }

    Allocator::Allocator(){
        this->is_allc = true;
        this->allc = new(struct allocator);
        allocator_init(this->allc);
    }

    Allocator::~Allocator(){
        allocator_end(this->allc);
        if (this->is_allc)
            delete(this->allc);
        else 
            this->allc = nullptr;
    }



    struct ev_loop *EventLoop::get_c(){
        return this->loop;
    }

    EventLoop::EventLoop(struct ev_loop *loop){
        this->loop = loop;
    }

    EventLoop::EventLoop(Allocator &allc, ssize_t cores){
        this->is_allc = true;
        this->loop = new(struct ev_loop);
        loop_create(allc.get_c(), this->loop, cores);
    }

    EventLoop::~EventLoop(){
        loop_stop(this->loop);
        if (this->is_allc)
            delete(this->loop);
        else
            this->loop = nullptr;
    }

    void EventLoop::run(){
        loop_run(this->loop);
    }
}