#ifndef JXT_DB_H
#define JXT_DB_H
#include "Page.h"
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <boost/lockfree/queue.hpp>
#include "ThreadPool.h"
#include <map>
#include <set>
namespace DB{
class PageManagement{
public:
    PageManagement(Setting setting);
    ~PageManagement();
    bool ValidMemPageID(uint32_t page_id){
        if(page_id > mpage_num_) return false;
        return true;
    }
    Page* GetMemPage(uint32_t page_id){
        off_t off = static_cast<off_t>(page_id) * setting_.page_size_;
        Page* page = (Page*)(mstart_ + off);
        return page;
    }
    void InitPageMetaArray();
    Page* GetPage(PageMeta* p);
    bool Scan(std::string key,PageMeta* p,std::vector<Item*>&result);
    bool Get(std::string key,std::vector<Item*>&result);
    bool Set(std::string key,std::vector<std::string>&result);
    bool Set(std::string key,std::vector<std::pair<uint32_t ,std::string>>&result);
    bool Write2Disk();
    void CopyMete(PageMeta* d_meta,PageMeta* s_meta);
    PageMeta* GetMemFreePage(){
        PageMeta* res;
        mfree_queue_->pop(res);
        res->free_ = 0;
        mused_queue_->push(res);
        return res;
    }
    bool IsFull(PageMeta * p){
        if(p->nalloc_ >= setting_.page_item_size_)return true;
        return false;
    }
    Item* WritableItem(const uint32_t page_id);
private:
    Setting setting_;
    uint8_t *mstart_;
    uint8_t *mend_;
    uint8_t *buf_;
    uint32_t buf_offset_;
    int fd_;
    off_t dstart_;
    off_t dend_;
    Thread_Pool pool_;
    uint32_t mpage_num_;
    uint32_t dpage_num_;
    uint32_t buf_page_num_;
    // 内存中PageMeta的表
    PageMeta *mstable_;
    // SSD中PageMeta的表
    PageMeta *dstable_;
   // std::map<std::string,>
   std::atomic<uint32_t> nmfree_num_;
   boost::lockfree::queue<PageMeta*> *mfree_queue_;
   std::atomic<uint32_t> nmused_num_;
   boost::lockfree::queue<PageMeta*> *mused_queue_;
   std::atomic<uint32_t> ndfree_num_;
   boost::lockfree::queue<PageMeta*> *dfree_queue_;
   std::atomic<uint32_t> ndused_num_;
   boost::lockfree::queue<PageMeta*> *dused_queue_;
   std::map<std::string,std::set<PageMeta*>>index_;
   std::map<std::string,PageMeta*>used_page_;
};
}
#endif //JXT_DB_H