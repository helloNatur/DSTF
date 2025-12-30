#include "PageManagement.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <random>
using namespace DB;
std::string GenerateRandomString(size_t length) {
    // 定义字符集（字母和数字）
    const std::string characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    // 获取字符集的长度
    size_t charactersLength = characters.size();

    // 创建随机数生成器
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, charactersLength - 1);

    // 生成随机字符串
    std::string randomString;
    for (size_t i = 0; i < length; ++i) {
        randomString += characters[dist(rng)];
    }

    return randomString;
}

class PageTest{
public:
    void setup() {
        const off_t size = 1L * 1024 * 1024 * 1024; // 1GB
        path = "/home/baum/encdb/JXT/SSD/page_ssd";
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
            perror("open failed");
            return;
        }
        if (ftruncate(fd, size) == -1) {
            perror("ftruncate failed");
            close(fd);
            return;
        }
        close(fd);
        
        setting = new Setting();
        setting->ssd_device_ = "/home/baum/encdb/JXT/SSD/page_ssd";
        page_m = new PageManagement(*setting);
    }
    void SetTest(uint32_t num){
        for(uint32_t i=0;i<num;i++){
            auto key = GenerateRandomString(32);
            std::vector<std::pair<uint32_t ,std::string>>key_array;
            for(uint32_t j=0;j<num;j++){
                 key_array.emplace_back(std::make_pair(j,key));
            }
            page_m->Set(key,key_array);
            std::vector<Item*>res;
            page_m->Write2Disk();
            page_m->Get(key,res);
            for(uint32_t j=0;j<res.size();j++){
                std::cout<<res[j]->row_id_<<" "<<std::string(reinterpret_cast<const char*>(res[j]->value_),32)<<std::endl;
            }
            std::cout<<"========================================\n";
            key_array.clear();
        }
    }
    std::string path;
    Setting *setting;
    PageManagement* page_m;
};
int main(){
    PageTest pt;
    pt.setup();
    pt.SetTest(10000);
    std::cout<<"hello\n";
    return 0;
}