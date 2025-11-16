#pragma once

#include <asyncio.h>
#include <allocator.h>
#include <logger.h>

namespace zl {
    
    class Future {
        struct future *_fut = nullptr;

        public:

        struct future *get_c();

        template<class T>
        T* wait(){
            return static_cast<T*>(await(this->_fut));
        }

        Future(
            struct future *fut
        );

        Future();
        ~Future();
    };

    class Allocator {
        bool is_allc = false;
        struct allocator *allc = nullptr;

        public:

        struct allocator *get_c();

        template<class T> 
        T* malloc(size_t bytes){
            return static_cast<T*>(alc_malloc(
                this->allc,
                bytes
            ));
        }

        template<class T>
        T* realloc(T* ptr, size_t bytes){
            return static_cast<T*>(alc_realloc(
                this->allc,
                ptr,
                bytes
            ));
        }

        template<class T>
        T* calloc(size_t count, size_t el_size){
            return static_cast<T*>(alc_calloc(
                this->allc,
                count,
                el_size
            ));
        }

        void free(void *ptr);

        Allocator(struct allocator *allc);
        Allocator(const Allocator &other) = delete;

        Allocator();
        ~Allocator();
    };

    class EventLoop {
        bool is_allc = false;
        struct ev_loop *loop = nullptr;

        public:
        
        struct ev_loop *get_c();
        void run();

        EventLoop(struct ev_loop *loop);
        EventLoop(Allocator &allc, ssize_t cores);

        EventLoop();
        ~EventLoop();
    };
};