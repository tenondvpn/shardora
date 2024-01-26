#pragma once

#include <linux/compiler.h>

template<typename T>
T load_consume(T const* addr) {
    T v = *const_cast<T const volatile*>(addr);
    __memory_barrier();
    return v;
}

template<typename T>
void store_release(T* addr, T v) {
    __memory_barrier();
    *const_cast<T volatile*>(addr) = v;
}

size_t const cache_line_size = 64;

template<typename T>
class spsc_queue {
public:
    spsc_queue() {
        node* n = new node;
        n->next_ = 0;
        tail_ = head_ = first_ = tail_copy_ = n;
    }

    ~spsc_queue() {
        node* n = first_;
        do {
            node* next = n->next_;
            delete n;
            n = next;
        } while (n);
    }

    void enqueue(T v) {
        node* n = alloc_node();
        n->next_ = 0;
        n->value_ = v;
        store_release(&head_->next_, n);
        head_ = n;
    }

    bool dequeue(T& v) {
        if (load_consume(&tail_->next_)) {
            v = tail_->next_->value_;
            store_release(&tail_, tail_->next_);
            return true;
        } else {
            return false;
        }
    }

private:
    struct node {
        node* next_;
        T value_;
    };

    node* tail_;
    char cache_line_pad_[cache_line_size];
    node* head_;
    node* first_;
    node* tail_copy_;

    node* alloc_node() {
        if (first_ != tail_copy_) {
            node* n = first_;
            first_ = first_->next_;
            return n;
        }

        tail_copy_ = load_consume(&tail_);
        if (first_ != tail_copy_) {
            node* n = first_;
            first_ = first_->next_;
            return n;
        }
        node* n = new node;
        return n;
    }

    spsc_queue(spsc_queue const&);
    spsc_queue& operator = (spsc_queue const&);
};
