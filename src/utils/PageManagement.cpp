#include "PageManagement.h"
namespace DB{
    void *tsmmap(uint32_t size){
        void *p;
        p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return p;
    }

    int tsmunmap(void *p, std::size_t size) {
        int status;
        status = munmap(p, size);
        return status;
    }

    bool SSD_DevieSize(const char *path, size_t *size) {
        int status;
        struct stat statiofo;
        int fd;

        status = stat(path, &statiofo);
        if (!S_ISREG(statiofo.st_mode) && !S_ISBLK(statiofo.st_mode)) {
        return false;
        }
        if (S_ISREG(statiofo.st_mode)) {
        *size = static_cast<size_t>(statiofo.st_size);
        return true;
        }

        fd = open(path, O_RDONLY, 0644);
        if (fd < 0) {
        return false;
        }
        status = ioctl(fd, _IOR(0x12,114,size_t), size);
        if (status < 0) {
        close(fd);
        return false;
        }
        close(fd);
        return true;
    }

    PageManagement::PageManagement(Setting setting):pool_(16)
    {
        setting_ = setting;
        mpage_num_ = setting_.total_page_memory_ / setting_.page_size_;
        mstart_ = static_cast<uint8_t *>(tsmmap(setting_.total_page_memory_));
        if (mstart_ == MAP_FAILED) {
            throw std::runtime_error("mmap failed for mstart_");
        }
        mend_   = mstart_ + setting_.total_page_memory_;
        buf_ = static_cast<uint8_t *>(tsmmap(setting_.total_page_memory_/10));
        if (buf_ == MAP_FAILED) {
            tsmunmap(mstart_, setting_.total_page_memory_);
            throw std::runtime_error("mmap failed for buf_");
        }
        buf_offset_ = 0;
        nmfree_num_.store(0);
        ndfree_num_.store(0);
        nmused_num_.store(0);
        ndused_num_.store(0);

        fd_ = open(setting_.ssd_device_, O_RDWR | O_DIRECT, 0644);
        if (fd_ < 0) {
            tsmunmap(mstart_, setting_.total_page_memory_);
            tsmunmap(buf_, setting_.total_page_memory_ / 10);
            throw std::logic_error("fail in open file");
        }

        size_t size = 0;
        if (!SSD_DevieSize(setting_.ssd_device_, &size)) {
            close(fd_);
            tsmunmap(mstart_, setting_.total_page_memory_);
            tsmunmap(buf_, setting_.total_page_memory_ / 10);
            throw std::runtime_error("failed to get SSD device size");
        }
        
        dpage_num_ = size / setting_.page_size_;
        dstart_ = 0;
        dend_ = dstart_ + size;

        mfree_queue_ = new boost::lockfree::queue<PageMeta*>{mpage_num_};
        mused_queue_ = new boost::lockfree::queue<PageMeta*>{mpage_num_};
        dfree_queue_ = new boost::lockfree::queue<PageMeta*>{dpage_num_};
        dused_queue_ = new boost::lockfree::queue<PageMeta*>{dpage_num_};

        if (!mfree_queue_ || !mused_queue_ || !dfree_queue_ || !dused_queue_) {
            delete mfree_queue_;
            delete mused_queue_;
            delete dfree_queue_;
            delete dused_queue_;
            close(fd_);
            tsmunmap(mstart_, setting_.total_page_memory_);
            tsmunmap(buf_, setting_.total_page_memory_ / 10);
            throw std::runtime_error("failed to allocate queue objects");
        }

        InitPageMetaArray();
    }

    PageManagement::~PageManagement() {
        delete dfree_queue_;
        delete mfree_queue_;
        delete dused_queue_;
        delete mused_queue_;
    }
   void PageManagement::InitPageMetaArray(){
        PageMeta *page_meta;
        //====================初始化内存中slab info============================
        mstable_ = static_cast<PageMeta *>((void *)malloc(sizeof(*mstable_) * mpage_num_));
        if (mstable_ == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < mpage_num_; i++) {
            page_meta = &mstable_[i];
            page_meta->page_id_ = i;
            page_meta->nalloc_ = 0;
            page_meta->free_ = true;
            // sinfo->addr_ = i;
            page_meta->mem_ = 1;
            mfree_queue_->push(page_meta);
        }
        //====================初始化SSD中slab info============================
        dstable_ = static_cast<PageMeta *>((void *)malloc(sizeof(*mstable_) * dpage_num_));
        if (dstable_ == nullptr) {
            return ;
        }

        for (uint32_t i = 0; i < mpage_num_; i++) { //这里是否应该使用dpage_num_
            page_meta = &dstable_[i];
            page_meta->page_id_ = i;
            page_meta->nalloc_ = 0;
            page_meta->mem_ = 0;
            page_meta->free_ = true;
            dfree_queue_->push(page_meta);
        }
        return;
    }
    Item *PageManagement::WritableItem(const uint32_t page_id) {
       if(!ValidMemPageID(page_id)){
           return nullptr;
       }
       PageMeta* page_meta = &mstable_[page_id];
       Page* page = GetMemPage(page_id);
       Item *item = (Item*)(page + page_meta->nalloc_*ITEM_SIZE);
       item->row_id_ = page_meta->nalloc_;
       page_meta->nalloc_++;
       return  item;
    }

    bool PageManagement::Set(std::string key, std::vector<std::string> &result) {
        PageMeta* page_meta;
        if(index_.find(key) == index_.end()){
            std::set<PageMeta*>p_meta_v;
            index_[key] = p_meta_v;
            page_meta = GetMemFreePage();
            used_page_[key] = page_meta;
            page_meta->key_ = key;
            index_[key].insert(page_meta);
        }else{
            page_meta = used_page_[key];
            //page_meta = index_[key].at(index_[key].size()-1);
            if(IsFull(page_meta)){
                page_meta = GetMemFreePage();
                used_page_[key] = page_meta;
                index_[key].insert(page_meta);
                page_meta->key_ = key;
                index_[key].insert(page_meta);
            }
        }
        for(auto v:result){
            if(IsFull(page_meta)){
                page_meta = GetMemFreePage();
                used_page_[key] = page_meta;
                index_[key].insert(page_meta);
                page_meta->key_ = key;
                index_[key].insert(page_meta);
            }
            auto item = WritableItem(page_meta->page_id_);
            memcpy(item->value_,&v.c_str()[0],(ITEM_SIZE-4));
        }
        return true;
    }

    bool PageManagement::Set(std::string key, std::vector<std::pair<uint32_t, std::string>> &result) {
        PageMeta* page_meta;
        if(index_.find(key) == index_.end()){
            std::set<PageMeta*>p_meta_v;
            index_[key] = p_meta_v;
            page_meta = GetMemFreePage();
            used_page_[key] = page_meta;
            page_meta->key_ = key;
            index_[key].insert(page_meta);
        }else{
            page_meta = used_page_[key];
            //page_meta = index_[key].at(index_[key].size()-1);
            if(IsFull(page_meta)){
                page_meta = GetMemFreePage();
                used_page_[key] = page_meta;
                index_[key].insert(page_meta);
                page_meta->key_ = key;
                index_[key].insert(page_meta);
            }
        }
        for(auto v:result){
            if(IsFull(page_meta)){
                page_meta = GetMemFreePage();
                used_page_[key] = page_meta;
                index_[key].insert(page_meta);
                page_meta->key_ = key;
                index_[key].insert(page_meta);
            }
            auto item = WritableItem(page_meta->page_id_);
            item->row_id_ = v.first;
            memcpy(item->value_,&v.second.c_str()[0],(ITEM_SIZE-4));
        }
        return true;
    }

    bool PageManagement::Get(std::string key, std::vector<Item*> &result) {
        if(index_.find(key) == index_.end()){
            return false;
        }
        for(auto &it:index_[key]){
            Scan(key,it,result);
        }
        return true;
    }

    Page *PageManagement::GetPage(DB::PageMeta *p) {
        if(!p){
            return nullptr;
        }
        if(p->mem_){
            return (Page*)(mstart_ + PAGE_SIZE * p->page_id_);
        }else{
            off_t off = dstart_ + (static_cast<off_t>(p->page_id_*PAGE_SIZE));
            uint32_t offset = buf_offset_;
            buf_offset_ += PAGE_SIZE;
            auto n = pread(fd_, buf_, PAGE_SIZE, off);
            if(n < PAGE_SIZE){
                return nullptr;
            }
            return (Page*)(buf_ + offset);
        }
    }

    bool PageManagement::Scan(std::string key, PageMeta* p, std::vector<Item *>& result) {
        if(p == nullptr){
            return false;
        }
        Page* page = GetPage(p);
        for(uint32_t i=0; i<p->nalloc_ ;i++){
            Item* item = (Item*)(page + i*ITEM_SIZE);
            //std::cout<<item->row_id_<<" "<<std::string(reinterpret_cast<const char*>(item->value_),ITEM_SIZE-4)<<std::endl;
            if(memcmp(item->value_,key.c_str(),ITEM_SIZE-4)==0){
                result.emplace_back(item);
            }
        }
        return true;
    }

    bool PageManagement::Write2Disk() {
        for(uint32_t i=0;i<setting_.write_batch_size_;i++){
            PageMeta* mem_page_meta;
            mused_queue_->pop(mem_page_meta);
            if(!IsFull(mem_page_meta)){
                mused_queue_->push(mem_page_meta);
                return true;
            }
            Page* mem_page = (Page*)(mstart_ + PAGE_SIZE*mem_page_meta->page_id_);
            PageMeta* disk_page_meta;
            dfree_queue_->pop(disk_page_meta);
            off_t offset = dstart_ + disk_page_meta->page_id_*PAGE_SIZE;
            auto n = pwrite(fd_,mem_page,PAGE_SIZE,offset);
            if(n < PAGE_SIZE){
                throw std::logic_error("fail in Write2Disk");
            }
            index_[mem_page_meta->key_].erase(mem_page_meta);
            CopyMete(disk_page_meta,mem_page_meta);
            index_[mem_page_meta->key_].insert(disk_page_meta);
        }
        return true;
    }

    void PageManagement::CopyMete(DB::PageMeta *d_meta, DB::PageMeta *s_meta) {
        d_meta->key_ = s_meta->key_;
        d_meta->nalloc_ = s_meta->nalloc_;
        d_meta->free_ = s_meta->nalloc_;
    }
}
