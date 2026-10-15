#pragma once
#include <vector>
#include <ranges>

namespace ycnet::detail {

template <typename value_type>
concept producer_owned_type = requires {
    value_type::thread_id;
};

/**
 * \brief threads... <-> one thread only (main_thread?)
 * 생산자별 버퍼를 하나의 소비자가 읽는 객체 저장소입니다.
 * 현재 volatile 상태 공유에는 C++ 메모리 모델에 따른 동기화 보장이 없습니다.
 * [*** 중요! ***] object를 read하는 thread는 한개여야 합니다.
 * [*** 중요! ***] object를 write하는 thread는 여러개 일 수 있습니다.
 * \tparam value_type value_type of thread safe object
 * \tparam thread_count Number of threads, default is 1
 * \tparam max_items Maximum number of items per producer, default is 8196
 */
template <producer_owned_type value_type, int thread_count = 1, int max_items = 8196>
class mpsc_buffer {
    std::vector<int> index_q_[thread_count];
    std::vector<int> r_index_q_;
    std::vector<value_type> mem_;
    std::vector<value_type*> mem_ptr_;
    volatile bool used_[max_items * thread_count] {};
    
    int find_first_false(int thread_id) {
        auto& iq = index_q_[thread_id];
        if(iq.empty()) {
            for(int i = 0; i < max_items; ++i) {
                if(!used_[thread_id * max_items + i]) iq.push_back(max_items * thread_id + i);
            }
        }
        if(iq.empty()) return -1;
        auto i = iq.back();
        iq.pop_back();
        return i;
    }
public:
    explicit mpsc_buffer() : mem_(max_items * thread_count), mem_ptr_(max_items * thread_count) {
        for (int i = 0; i < thread_count; ++i) {
            for (int j = 0; j < max_items; ++j) 
                mem_[i * max_items + j].thread_id = i;
            index_q_[i].reserve(max_items);
        }
    }
    bool try_push(const int thread_id, value_type*& out) {
        const int idx = find_first_false(thread_id);
        if(idx == -1) return false;
        out = &mem_[idx];
        return true;
    }

    /**
     * \brief 이 함수를 실행했다면 더이상 obj를 참조해서는 안됍니다.
     * \param obj read thread가 권한을 가질 수 있게 되는 객체
     */
    void make_readable(value_type*& obj) {
        auto idx = (obj - mem_.data()) % max_items;
        used_[obj->thread_id * max_items + idx] = true;
        obj = nullptr;
    }

    /**
     * \brief read thread 에서 객체에 참조를 그만합니다.
     * 함수를 호출하고 나서 반환한 obj에 절대로 접근해서는 안됍니다.
     * \param obj write thread 가 객체에 쓸 수 있도록 객체를 반환합니다.
     */
    void read_end(value_type*& obj) {
        auto idx = (obj - mem_.data()) % max_items;
        used_[obj->thread_id * max_items + idx] = false;
        obj = nullptr;
    }
    
    auto& get_read_ranges() {
        mem_ptr_.clear();
        for(auto& i : std::views::filter(used_, [](auto& b) { return b; })) {
            mem_ptr_.push_back(&mem_[&i - used_]);
        }
        return mem_ptr_;
    }
};


/* example
 ===============================
struct user_info {
    int id;
    int endpoint;
    int thread_id;
};

int main(int argc, char* argv[]) {
    mpsc_buffer<user_info, 4> mem;
    user_info* ptr;
    
    mem.try_push(0, ptr);
    ptr->id = 1;
    ptr->endpoint = 2;
    mem.make_readable(ptr);

    for(auto i : mem.get_read_ranges()) {
        printf("%d %d\n", i->id, i->endpoint);
        mem.read_end(i);
    }
}
===============================
*/

} // namespace ycnet::detail
