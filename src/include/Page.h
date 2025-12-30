#ifndef JXT_PAGE_H
#define JXT_PAGE_H
#include <cstdint>
#include <stdexcept>
#include <iostream>
namespace DB{
#define KB (1024)
#define MB (1024 * KB)
#define GB (1024 * MB)
#define TOTAL_PAGE_MEMORY 1024 * MB
#define PAGE_SIZE 4 * KB
#define READ_BUFFER_SIZE 256 * MB
#define DISK_READ_GRAN 512
#define ROUND_UP(x, step) (((x) + (step)-1) / (step) * (step))
#define ITEM_SIZE 36
struct Item{
    uint32_t row_id_;
    uint8_t value_[1];
};
struct Page{
    uint8_t data_[1];
    Page() = default;
};
struct PageMeta{
    uint32_t page_id_;
    bool mem_;
    bool free_;
    uint32_t nalloc_;
    std::string key_;
};
class Setting {
public:
    Setting() { SetDefault(); }
    size_t total_page_memory_;
    char *ssd_device_;
    size_t page_size_;
    size_t page_item_size_;
    int port;
    char const *addr;
    uint32_t write_batch_size_;
//    uint32_t migrate_batch_size_;

private:
    void SetDefault() {
        total_page_memory_ = TOTAL_PAGE_MEMORY;
        ssd_device_ = "/tmp/ssd";
        port = 11211;
        addr = "";
        page_size_ = PAGE_SIZE;
        page_item_size_ = PAGE_SIZE / ITEM_SIZE;
        write_batch_size_ = 160;
//        migrate_batch_size_ = 6400;
    }
};
}


#endif //JXT_PAGE_H
