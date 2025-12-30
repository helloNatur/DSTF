// Copyright (c) Simon Fraser University & The Chinese University of Hong Kong. All rights reserved.
// Licensed under the MIT license.

/**
 * 
 * 测mix需要在search, delete中加negotiate 
 *  测full不要
 */

#include <gflags/gflags.h>
#include <immintrin.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

#ifndef PEA_ONLY
#include "../src/CCEH/CCEH_baseline.h"
#include "../src/Dash-16/ex_finger.h"
#include "../src/Dash-512/ex_finger.h"
#include "../src/Level/level_base.h"
#include "../src/CLHT/clht_lb_res.h"
#endif
#include "../src/PeaHash/pea_hash.h"
#include "../src/PeaHash/space_manager.h"
#include "../util/key_generator.hpp"
#include "../util/uniform.hpp"
#include "SegmentTree.h"
#include "TimeUtil.h"
#include <memory>

std::string pool_name = "/mnt/mypmem1/liuzhuoxuan/";
// std::string pool_name = "/home/liuzhuoxuan/workspace/peahash-test/workload/";
DEFINE_string(index, "pea",
              "the index to evaluate:pea/dash-16/dash-512/cceh/level/clht");
DEFINE_string(k, "fixed", "the type of stored keys: fixed/variable");
DEFINE_string(distribution, "uniform",
              "The distribution of the workload: uniform/skew");
DEFINE_uint64(i, 256, "the initial number of segments in pea/extendible hashing");
DEFINE_uint64(t, 1, "the number of concurrent threads");
DEFINE_uint64(n, 1000000, "the number of pre-insertion load");
DEFINE_uint64(loadType, 0, "type of pre-load integers: random (0) - range (1)");
DEFINE_uint64(p, 69000000,
              "the number of operations(insert/search/deletion) to execute");
DEFINE_string(
        op, "full",
        "which type of operation to execute:insert/pos/neg/delete/mixed/skew-all/full/skew-search/date-hash");
DEFINE_double(r, 0.8, "read ratio for mixed workload:0~1.0");
DEFINE_double(s, 0.2, "insert ratio for mixed workload: 0~1.0");
DEFINE_double(d, 0, "delete ratio for mixed workload:0~1.0");
DEFINE_double(skew, 0.8, "skew factor of the workload");
DEFINE_uint32(e, 1, "whether register epoch in application level:0/1");
DEFINE_uint32(ms, 100, "#miliseconds to sample the operations");
DEFINE_uint32(vl, 16, "the length of the variable length key");
DEFINE_uint64(ps, 10ul, "The size of the memory pool (GB)");
DEFINE_uint64(ed, 5, "The frequency to enroll into the epoch");
DEFINE_string(csv_file, "table1_k7_j1_573703.csv", "CSV file path for date-based hash table test");

uint64_t initCap, thread_num, load_num, operation_num;
std::string operation;
std::string distribution;
std::string key_type;
std::string index_type;
int bar_a, bar_b, bar_c;
double read_ratio, insert_ratio, delete_ratio, skew_factor;
std::mutex mtx;
std::condition_variable cv;
bool finished = false;
bool open_epoch;
uint32_t msec, var_length;
struct timeval tv1, tv2, tv3;
size_t pool_size = 1024ul * 1024ul * 1024ul * 5ul;
key_generator_t *uniform_generator;
uint64_t EPOCH_DURATION;
uint64_t load_type = 0;

struct operation_record_t {
    uint64_t number;
    uint64_t dummy[7]; /*patch to a cacheline size, avoid false sharing*/
};

operation_record_t operation_record[1024];

struct range {
    int index;
    uint64_t begin;
    uint64_t end;
    int length; /*if this is the variable length key, use this parameter to
                 indicate the length of the key*/
    void *workload;
    uint64_t random_num;
    struct timeval tv;
};

void set_affinity(uint32_t idx) {
    cpu_set_t my_set;
    CPU_ZERO(&my_set);
    CPU_SET(2 * idx + 1, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
}

template<class T>
Hash<T> *InitializeIndex(int seg_num) {
    Hash<T> *eh;
    bool file_exist = false;
    gettimeofday(&tv1, NULL);
#ifndef PEA_ONLY
    if (index_type == "dash-16") {
        std::cout << "Dash-16" << std::endl;
#ifdef PREALLOC
        extendible_16::TlsTablePool<Key_t>::Initialize();
#endif
        eh = new extendible_16::Finger_EH<T>(seg_num);
    } else if (index_type == "dash-512") {
        std::cout << "Dash-512" << std::endl;
#ifdef PREALLOC
        extendible_512::TlsTablePool<Key_t>::Initialize();
#endif
        eh = new extendible_512::Finger_EH<T>(8);
    } else if (index_type == "cceh") {
        std::cout << "CCEH" << std::endl;
        eh = new cceh::CCEH<T>(seg_num);
    } else if (index_type == "level") {
        std::cout << "Level Hashing" << std::endl;
        eh = new level::LevelHashing<T>();
        int level_size = 14;
        level::initialize_level(reinterpret_cast<level::LevelHashing<T> *>(eh),
                                &level_size);
    } else if (index_type == "clht") {
        std::cout << "CLHT" << std::endl;
        eh = new clht_lb::CLHT<T>(81920);
    } else 
#endif
    if (index_type == "pea") {
        std::cout << "Pea" << std::endl;
        std::string index_pool_name = pool_name + "pmem_pea.data";
        if (FileExists(index_pool_name.c_str())) file_exist = true;
        SpaceManager::Initialize(index_pool_name.c_str(), pool_size, thread_num);
        size_t pea_size = 8;
        eh = new pea::PeaHashing<T>(pea_size, thread_num);
    } 
    if (operation == "recovery") {
        gettimeofday(&tv3, NULL);  // test end
        eh->Recovery();
        gettimeofday(&tv2, NULL);  // test end
        double duration = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
                          (double) (tv2.tv_sec - tv1.tv_sec);
        double scanning_time = (double) (tv2.tv_usec - tv3.tv_usec) / 1000000 +
                               (double) (tv2.tv_sec - tv3.tv_sec);
        std::cout << "The recovery algorithm time = " << scanning_time << std::endl;
        std::cout << "Total recovery time (open pool + recovery algorithm) = "
                  << duration << std::endl;
    }

    return eh;
}

/*generate 8-byte number and store it in the memory_region*/
void generate_8B(void *memory_region, uint64_t generate_num, bool persist,
                 key_generator_t *key_generator) {
    uint64_t *array = reinterpret_cast<uint64_t *>(memory_region);

    for (uint64_t i = 0; i < generate_num; ++i) {
        array[i] = key_generator->next_uint64();
    }
}

void skew_generate_8B(void *memory_region, uint64_t generate_num, bool persist,
                      zipfian_key_generator_t *key_generator) {
    uint64_t *array = reinterpret_cast<uint64_t *>(memory_region);

    for (uint64_t i = 0; i < generate_num; ++i) {
        array[i] = key_generator->next_uint64_skew(4999);
    }
}

/*generate 16-byte string and store it in the memory_region*/
void generate_16B(void *memory_region, uint64_t generate_num, int length,
                  bool persist, key_generator_t *key_generator) {
    string_key *var_key;
    uint64_t *_key;
    uint64_t random_num;
    char *workload = reinterpret_cast<char *>(memory_region);

    int word_num = (length / 8) + (((length % 8) != 0) ? 1 : 0);
    _key = reinterpret_cast<uint64_t *>(malloc(word_num * sizeof(uint64_t)));

    for (uint64_t i = 0; i < generate_num; ++i) {
        var_key = reinterpret_cast<string_key *>(workload +
                                                 i * (length + sizeof(string_key)));
        var_key->length = length;
        random_num = key_generator->next_uint64();
        for (int j = 0; j < word_num; ++j) {
            _key[j] = random_num;
        }
        memcpy(var_key->key, _key, length);
    }
}

template<class T>
void Load(int kv_num, Hash<T> *index, int length, void *workload) {
    std::cout << "Start load warm-up workload" << std::endl;
    if (kv_num == 0) return;
    std::string fixed("fixed");
    T *_worklod = reinterpret_cast<T *>(workload);
    T key;
    if constexpr (!std::is_pointer_v<T>) {
        for (uint64_t i = 0; i < kv_num; ++i) {
            index->Insert(_worklod[i], DEFAULT, true);
        }
    }
    std::cout << "Finish loading " << kv_num << " keys" << std::endl;
}

inline void spin_wait() {
    SUB(&bar_b, 1);
    while (LOAD(&bar_a) == 1); /*spinning*/
}

inline void end_notify(struct range *rg) {
    gettimeofday(&rg->tv, NULL);
    if (SUB(&bar_c, 1) == 0) {
        std::unique_lock<std::mutex> lck(mtx);
        finished = true;
        cv.notify_one();
    }
}

inline void end_sub() { SUB(&bar_c, 1); }

template<class T>
void concurr_insert_without_epoch(struct range *_range, Hash<T> *index) {
    set_affinity(_range->index);
    int begin = _range->begin;
    int end = _range->end;
    char *workload = reinterpret_cast<char *>(_range->workload);
    T key;

    spin_wait();
    T *key_array = reinterpret_cast<T *>(workload);
    for (uint64_t i = begin; i < end; ++i) {
        index->Insert(key_array[i], DEFAULT);
    }

    end_notify(_range);
}


template<class T>
void concurr_search_without_epoch(struct range *_range, Hash<T> *index) {
    set_affinity(_range->index);
    int begin = _range->begin;
    int end = _range->end;
    char *workload = reinterpret_cast<char *>(_range->workload);
    T key;
    uint64_t not_found = 0;

    spin_wait();

    if constexpr (!std::is_pointer_v<T>) {
        T *key_array = reinterpret_cast<T *>(workload);
        for (uint64_t i = begin; i < end; ++i) {
            if (index->Get(key_array[i]) == NONE) {
                not_found++;
            }
        }
    } else {
        T var_key;
        uint64_t string_key_size = sizeof(string_key) + _range->length;
        for (uint64_t i = begin; i < end; ++i) {
            var_key = reinterpret_cast<T>(workload + string_key_size * i);
            if (index->Get(var_key) == NONE) {
                not_found++;
            }
        }
    }
    std::cout << "not_found = " << not_found << std::endl;
    end_notify(_range);
}

template<class T>
void concurr_delete_without_epoch(struct range *_range, Hash<T> *index) {
    set_affinity(_range->index);
    int begin = _range->begin;
    int end = _range->end;
    char *workload = reinterpret_cast<char *>(_range->workload);
    T key;
    uint64_t not_found = 0;

    spin_wait();
    if constexpr (!std::is_pointer_v<T>) {
        T *key_array = reinterpret_cast<T *>(workload);
        for (uint64_t i = begin; i < end; ++i) {
            if (index->Delete(key_array[i]) == false) {
                not_found++;
            }
        }
    } else {
        T var_key;
        int string_key_size = sizeof(string_key) + _range->length;
        for (uint64_t i = begin; i < end; ++i) {
            var_key = reinterpret_cast<T>(workload + string_key_size * i);
            if (index->Delete(var_key) == false) {
                not_found++;
            }
        }
    }
    // std::cout << "not_found = " << not_found << std::endl;
    end_notify(_range);
}

template<class T>
void mixed_without_epoch(struct range *_range, Hash<T> *index) {
    set_affinity(_range->index);
    uint64_t begin = _range->begin;
    uint64_t end = _range->end;
    char *workload = reinterpret_cast<char *>(_range->workload);
    T *key_array = reinterpret_cast<T *>(_range->workload);
    T key;
    int string_key_size = sizeof(string_key) + _range->length;

    UniformRandom rng(_range->random_num);
    uint32_t random;
    uint32_t not_found = 0;

    uint32_t insert_sign = (uint32_t) (insert_ratio * 100);
    uint32_t read_sign = (uint32_t) (read_ratio * 100) + insert_sign;
    uint32_t delete_sign = (uint32_t) (delete_ratio * 100) + read_sign;

    spin_wait();

    for (uint64_t i = begin; i < end; ++i) {
        if constexpr (std::is_pointer_v<T>) { /* variable length*/
            key = reinterpret_cast<T>(workload + string_key_size * i);
        } else {
            key = key_array[i];
        }

        random = rng.next_uint32() % 100;
        if (random < insert_sign) { /*insert*/
            index->Insert(key, DEFAULT);
        } else if (random < read_sign) { /*get*/
            if (index->Get(key) == NONE) {
                not_found++;
            }
        } else { /*delete*/
            index->Delete(key);
        }
    }
    // std::cout << "not_found = " << not_found << std::endl;
    /*the last thread notify the main thread to wake up*/
    end_notify(_range);
}

template<class T>
void GeneralBench(range *rarray, Hash<T> *index, int thread_num,
                  uint64_t operation_num, std::string profile_name,
                  void (*test_func)(struct range *, Hash<T> *)) {
    std::thread *thread_array[1024];
    profile_name = profile_name + std::to_string(thread_num);
    double duration;
    finished = false;
    bar_a = 1;
    bar_b = thread_num;
    bar_c = thread_num;

    std::cout << profile_name << std::endl;
    //  System::profile(profile_name, [&]() {
    for (uint64_t i = 0; i < thread_num; ++i) {
        thread_array[i] = new std::thread(*test_func, &rarray[i], index);
    }

    while (LOAD(&bar_b) != 0);                                     // Spin
    std::unique_lock<std::mutex> lck(mtx);  // get the lock of condition variable

    gettimeofday(&tv1, NULL);
    STORE(&bar_a, 0);  // start test
    while (!finished) {
        cv.wait(lck);  // go to sleep and wait for the wake-up from child threads
    }
    gettimeofday(&tv2, NULL);  // test end

    for (int i = 0; i < thread_num; ++i) {
        thread_array[i]->join();
        delete thread_array[i];
    }

    double longest = (double) (rarray[0].tv.tv_usec - tv1.tv_usec) / 1000000 +
                     (double) (rarray[0].tv.tv_sec - tv1.tv_sec);
    double shortest = longest;
    duration = longest;

    for (int i = 1; i < thread_num; ++i) {
        double interval = (double) (rarray[i].tv.tv_usec - tv1.tv_usec) / 1000000 +
                          (double) (rarray[i].tv.tv_sec - tv1.tv_sec);
        duration += interval;
        if (shortest > interval) shortest = interval;
        if (longest < interval) longest = interval;
    }
    //std::cout << "The time difference is " << longest - shortest << std::endl;
    duration = duration / thread_num;
    printf(
            "%f,%f,%f\n",
            operation_num / duration, operation_num / shortest, operation_num / longest);
    //  });
    // std::cout << profile_name << " End" << std::endl;
}

void *GenerateWorkload(uint64_t generate_num, int length) {
    /*Since there are both positive search and negative search, it should generate
     * 2 * generate_num workload*/
    void *workload;
    if (key_type == "fixed") {
        workload = malloc(generate_num * sizeof(uint64_t));
        generate_8B(workload, generate_num, false, uniform_generator);
    } else { /*Generate the variable lengh workload*/
        std::cout << "Genereate workload for variable length key " << std::endl;
        workload = malloc(generate_num * (length + sizeof(string_key)));
        generate_16B(workload, generate_num, length, false, uniform_generator);
        std::cout << "Finish Generation" << std::endl;
    }
    return workload;
}

void *GenerateSkewWorkload(uint64_t load_num, uint64_t exist_num,
                           uint64_t non_exist_num, int length) {
    void *workload;
    if (key_type == "fixed") {
        workload =
                malloc((load_num + exist_num + non_exist_num) * sizeof(uint64_t));
        uint64_t *fixed_workload = reinterpret_cast<uint64_t *>(workload);
        /* For the warm-up workload, it is generated using uniform generator*/
        if (load_type == 1) {
            key_generator_t *range_generator = new range_key_generator_t(1);
            generate_8B(fixed_workload, load_num, false, range_generator);
            delete range_generator;
        } else {
            generate_8B(fixed_workload, load_num, false, uniform_generator);
        }

        if (exist_num) {
            zipfian_key_generator_t *skew_generator =
                    new zipfian_key_generator_t(1, exist_num, skew_factor);
            if (operation == "skew-search") {
                skew_generate_8B(fixed_workload + load_num, exist_num, false, skew_generator);
            } else {
                generate_8B(fixed_workload + load_num, exist_num, false, skew_generator);
            }
            delete skew_generator;
        }

        if (non_exist_num) {
            key_generator_t *skew_generator = new zipfian_key_generator_t(
                    exist_num + load_num, exist_num + non_exist_num + load_num,
                    skew_factor);
            generate_8B(fixed_workload + load_num + exist_num, non_exist_num, false,
                        skew_generator);
            delete skew_generator;
        }
    } else { /*Generate the variable lengh workload*/
        std::cout << "Genereate workload for variable length key " << std::endl;
        workload = malloc((load_num + exist_num + non_exist_num) *
                          (length + sizeof(string_key)));
        if (load_type == 1) {
            key_generator_t *range_generator = new range_key_generator_t(1);
            generate_16B(workload, load_num, length, false, range_generator);
            delete range_generator;
        } else {
            generate_16B(workload, load_num, length, false, uniform_generator);
        }

        char *char_workload = reinterpret_cast<char *>(workload);
        if (exist_num) {
            key_generator_t *skew_generator =
                    new zipfian_key_generator_t(1, exist_num, skew_factor);
            generate_16B(char_workload + load_num * (length + sizeof(string_key)),
                         exist_num, length, false, skew_generator);
            delete skew_generator;
        }

        if (non_exist_num) {
            key_generator_t *skew_generator = new zipfian_key_generator_t(
                    exist_num + load_num, exist_num + non_exist_num + load_num,
                    skew_factor);
            generate_16B(char_workload +
                         (load_num + exist_num) * (length + sizeof(string_key)),
                         non_exist_num, length, false, skew_generator);
            delete skew_generator;
        }
        std::cout << "Finish Generation" << std::endl;
    }
    return workload;
}

void shuffle(uint64_t *workload, uint64_t number) {
    srand(time(nullptr));
    for (size_t i = 0; i < 2 * number; i++) {
        int replace_id = rand() % number;
        uint64_t swap_one = workload[replace_id];
        workload[replace_id] = workload[0];
        workload[0] = swap_one;
    }
}

template<class T>
void Run() {
    /* Index for Finger_EH*/
    uniform_generator = new uniform_key_generator_t();
    Hash<T> *index = InitializeIndex<T>(initCap);
    uint64_t generate_num = operation_num * 2 + load_num;
    /* Generate the workload and corresponding range array*/
    // std::cout << "Generate workload" << std::endl;
    void *workload;
    if (distribution == "uniform") {
        workload = GenerateWorkload(generate_num, var_length);
    } else {
        workload = GenerateSkewWorkload(load_num, operation_num, operation_num,
                                        var_length);
    }

    void *insert_workload;
    insert_workload = workload;
    // std::cout << "Finish Generate workload" << std::endl;

    // std::cout << "load num = " << load_num << std::endl;
    Load<T>(load_num, index, var_length, insert_workload);
    void *not_used_workload;
    void *not_used_insert_workload;

    if (key_type == "fixed") {
        uint64_t *key_array = reinterpret_cast<uint64_t *>(workload);
        not_used_workload = reinterpret_cast<void *>(key_array + load_num);
        not_used_insert_workload = not_used_workload;
    }

    /* Description of the workload*/
    srand((unsigned) time(NULL));
    struct range *rarray;
    uint64_t chunk_size = operation_num / thread_num;
    rarray = reinterpret_cast<range *>(malloc(thread_num * (sizeof(range))));
    for (int i = 0; i < thread_num; ++i) {
        rarray[i].index = i;
        rarray[i].random_num = rand();
        rarray[i].begin = i * chunk_size;
        rarray[i].end = (i + 1) * chunk_size;
        rarray[i].length = var_length;
        rarray[i].workload = not_used_workload;
    }
    rarray[thread_num - 1].end = operation_num;

    /* Benchmark Phase */
    std::cout << "Comprehensive Benchmark" << std::endl;
    std::cout << "insertion start" << std::endl;
    for (int i = 0; i < thread_num; ++i) {
        rarray[i].workload = not_used_insert_workload;
    }
    if (operation != "skew-all") {
        GeneralBench<T>(rarray, index, thread_num, operation_num, "Insert",
                        &concurr_insert_without_epoch);
    }
    // index->getNumber();
    shuffle((uint64_t *) not_used_workload, operation_num);
    for (int i = 0; i < thread_num; ++i) {
        rarray[i].workload = not_used_workload;
    }
    GeneralBench<T>(rarray, index, thread_num, operation_num, "Pos_search",
                    &concurr_search_without_epoch);
    for (int i = 0; i < thread_num; ++i) {
        rarray[i].begin = operation_num + i * chunk_size;
        rarray[i].end = operation_num + (i + 1) * chunk_size;
    }
    rarray[thread_num - 1].end = 2 * operation_num;
    GeneralBench<T>(rarray, index, thread_num, operation_num, "Neg_search",
                    &concurr_search_without_epoch);

    for (int i = 0; i < thread_num; ++i) {
        rarray[i].begin = i * chunk_size;
        rarray[i].end = (i + 1) * chunk_size;
    }
    rarray[thread_num - 1].end = operation_num;
    shuffle((uint64_t *) not_used_workload, operation_num);
    GeneralBench<T>(rarray, index, thread_num, operation_num, "Delete",
                    &concurr_delete_without_epoch);

    // index->getNumber();


    /*TODO Free the workload memory*/
}

// ==================== Date-based Hash Table with Segment Tree ====================

// CSV row data structure
struct CSVRow {
    std::string userid;
    std::string venueid;
    std::string venuecategoryid;
    std::string venuecategory;
    std::string latitude;
    std::string longitude;
    std::string timezoneoffset;
    std::string utctimestamp;
    std::string id;
    
    // Store the full line for reference
    std::string full_line;
};

// Note: Using existing SegmentTree from test/SegmentTree instead of custom DaySegmentTree

// Extract date from timestamp (e.g., "2012-04-03 18:17:18+00" -> "2012-04-03")
std::string extractDate(const std::string& timestamp) {
    size_t space_pos = timestamp.find(' ');
    if (space_pos != std::string::npos) {
        return timestamp.substr(0, space_pos);
    }
    return timestamp.substr(0, 10);  // Fallback: take first 10 characters
}

// Parse a CSV line into CSVRow structure
CSVRow parseCSVLine(const std::string& line) {
    CSVRow row;
    row.full_line = line;
    
    std::istringstream ss(line);
    std::string field;
    int field_idx = 0;
    
    while (std::getline(ss, field, ',')) {
        switch (field_idx) {
            case 0: row.userid = field; break;
            case 1: row.venueid = field; break;
            case 2: row.venuecategoryid = field; break;
            case 3: row.venuecategory = field; break;
            case 4: row.latitude = field; break;
            case 5: row.longitude = field; break;
            case 6: row.timezoneoffset = field; break;
            case 7: row.utctimestamp = field; break;
            case 8: row.id = field; break;
        }
        field_idx++;
    }
    
    return row;
}

// Create a date string key for hash table (fixed 10 bytes: "YYYY-MM-DD")
string_key* createDateKey(const std::string& date) {
    // date format: "2012-04-03" (10 characters)
    size_t key_size = sizeof(string_key) + 11;  // 10 chars + null terminator
    string_key* key = reinterpret_cast<string_key*>(malloc(key_size));
    key->length = 10;
    memcpy(key->key, date.c_str(), 10);
    key->key[10] = '\0';
    return key;
}

// Query function similar to bplus_tree->query_sql, but using PeaHash
// Returns tokens from SegmentTree within the time range [start_time, end_time]
std::vector<SegmentTree::IntervalResult> queryByTimeRange(
    Hash<string_key*>* date_hash,
    const std::map<std::string, std::shared_ptr<SegmentTree>>& segment_trees_map,
    const std::map<std::string, string_key*>& date_keys_map,
    const std::string& start_time, 
    const std::string& end_time,
    const std::vector<std::string>& keywords = {}) {
    std::vector<SegmentTree::IntervalResult> results;
    
    // Extract dates from timestamps
    std::string start_date = extractDate(start_time);
    std::string end_date = extractDate(end_time);
    
    // Convert to 10-minute intervals
    int start_interval = TimeUtil::time_to_10min_interval(start_time);
    int end_interval = TimeUtil::time_to_10min_interval(end_time);
    
    // If query spans multiple days, iterate through all dates
    std::vector<std::string> dates_to_query;
    
    if (start_date == end_date) {
        dates_to_query.push_back(start_date);
    } else {
        // Multi-day query: iterate through dates
        dates_to_query.push_back(start_date);
        dates_to_query.push_back(end_date);
        // TODO: Add logic to iterate through intermediate dates if needed
    }
    
    // Query each date
    for (const auto& date : dates_to_query) {
        // Use the same key object that was used during insertion
        string_key* date_key = nullptr;
        auto key_it = date_keys_map.find(date);
        if (key_it != date_keys_map.end()) {
            date_key = key_it->second;
        } else {
            // Fallback: create new key (but this might not work due to pointer comparison)
            date_key = createDateKey(date);
        }
        
        Value_t tree_ptr = date_hash->Get(date_key);
        
        if (tree_ptr != NONE) {
            // Get SegmentTree from map (using shared_ptr for lifetime management)
            auto it = segment_trees_map.find(date);
            if (it != segment_trees_map.end()) {
                std::shared_ptr<SegmentTree> segment_tree = it->second;
                
                int query_start = (date == start_date) ? start_interval : 0;
                int query_end = (date == end_date) ? end_interval : 143; // 144 intervals (0-143)
                
                // Get day timestamp for IntervalResult
                long long day_ts = TimeUtil::date_to_timestamp(date);
                
                // Query SegmentTree
                if (keywords.empty()) {
                    // Simple query: get all tokens in range
                    auto tokens = segment_tree->query(query_start, query_end);
                    if (!tokens.empty()) {
                        results.emplace_back(query_start, query_end, day_ts, tokens);
                    }
                } else {
                    // Query with keywords (spatial filtering)
                    auto interval_results = segment_tree->getCandidateIntervals(query_start, query_end, keywords);
                    for (auto& ir : interval_results) {
                        ir.day_ts = day_ts; // Set day timestamp
                        results.push_back(ir);
                    }
                }
            }
        }
        
        // Only free if we created a new key (not from date_keys_map)
        if (key_it == date_keys_map.end() && date_key != nullptr) {
            free(date_key);
        }
    }
    
    return results;
}

// Test function for date-based hash table with segment tree
// This function uses PeaHash to store date -> segment tree mapping
// Key: date string (e.g., "2012-04-03" extracted from "2012-04-03 18:17:18+00")
// Value: pointer to DaySegmentTree containing all data for that day
void testDateBasedHashTable() {
    std::cout << "========================================" << std::endl;
    std::cout << "Testing Date-based Hash Table with Segment Tree (PeaHash)" << std::endl;
    std::cout << "Reference: Similar to B+Tree in b_st_bf_test.cpp" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Example: For timestamp "2012-04-03 18:17:18+00", the key is "2012-04-03"
    std::string example_timestamp = "2012-04-03 18:17:18+00";
    std::string example_key = extractDate(example_timestamp);
    std::cout << "Example: For timestamp \"" << example_timestamp << "\"" << std::endl;
    std::cout << "        The key in PeaHash is: \"" << example_key << "\"" << std::endl;
    std::cout << "        Value: pointer to DaySegmentTree containing all data for " << example_key << std::endl;
    std::cout << std::endl;
    
    // Build phase - similar to b_st_bf_test.cpp SetUp()
    auto start_build = std::chrono::high_resolution_clock::now();
    
    std::string csv_file_path = FLAGS_csv_file;
    // Try absolute path first, then relative path
    if (csv_file_path.find('/') != 0 && csv_file_path.find("..") != 0) {
        // Try relative to data directory
        std::string data_path = "/home/shijw/JXT2/data/table1/" + csv_file_path;
        std::ifstream test_file(data_path);
        if (test_file.good()) {
            csv_file_path = data_path;
            test_file.close();
        } else {
            // Try relative to test directory
            std::string test_dir = std::string(__FILE__);
            size_t last_slash = test_dir.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                csv_file_path = test_dir.substr(0, last_slash + 1) + csv_file_path;
            }
        }
    }
    
    std::cout << "Reading CSV file: " << csv_file_path << std::endl;
    
    std::ifstream file(csv_file_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open CSV file: " << csv_file_path << std::endl;
        return;
    }
    
    // Initialize PeaHash for date -> segment tree mapping (in-memory dynamic hash table)
    // Similar to BPlusTree in b_st_bf_test.cpp, but using PeaHash instead
    std::string index_pool_name = pool_name + "pmem_pea_date.data";
    SpaceManager::Initialize(index_pool_name.c_str(), pool_size, 1);
    size_t pea_size = 8;
    Hash<string_key*> *date_hash = new pea::PeaHashing<string_key*>(pea_size, 1);
    
    // Map to store segment trees (in-memory, using shared_ptr for lifetime management)
    // Similar to: segment_tree = bplus_tree->search(day_timestamp)
    std::map<std::string, std::shared_ptr<SegmentTree>> segment_trees_map;
    
    // Map to store date keys (reuse same key object for same date to ensure pointer comparison works)
    std::map<std::string, string_key*> date_keys_map;
    
    // Map to store all CSV rows (in-memory)
    std::vector<CSVRow*> all_rows;
    
    std::string line;
    bool is_first_line = true;
    uint64_t row_count = 0;
    uint64_t date_count = 0;
    
    gettimeofday(&tv1, NULL);
    
    std::cout << "Building PeaHash/SegmentTree index..." << std::endl;
    
    // Read CSV file and build index
    while (std::getline(file, line)) {
        if (is_first_line) {
            // Skip header
            is_first_line = false;
            continue;
        }
        
        if (line.empty()) continue;
        
        // Remove trailing carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        CSVRow* row = new CSVRow(parseCSVLine(line));
        all_rows.push_back(row);
        
        // Extract date from timestamp (e.g., "2012-04-03" from "2012-04-03 18:17:18+00")
        std::string date = extractDate(row->utctimestamp);
        
        // Get or create segment tree for this date (similar to bplus_tree->search/insert)
        std::shared_ptr<SegmentTree> segment_tree;
        auto it = segment_trees_map.find(date);
        if (it == segment_trees_map.end()) {
            // Create new segment tree for this date
            // Similar to: segment_tree = std::make_shared<SegmentTree>(144, 0.001, 442);
            segment_tree = std::make_shared<SegmentTree>(144, 0.001, 442); // 每天144个10分钟间隔
            segment_trees_map[date] = segment_tree;
            date_count++;
            
            // Create and store date key (reuse same key object for same date)
            string_key* date_key = createDateKey(date);
            date_keys_map[date] = date_key;
            
            // Insert date key into PeaHash with pointer to segment tree
            // Similar to: bplus_tree->insert(day_timestamp, segment_tree)
            Value_t tree_ptr = reinterpret_cast<Value_t>(segment_tree.get());
            int insert_result = date_hash->Insert(date_key, tree_ptr);
            if (insert_result != 0) {
                std::cerr << "Warning: Insert failed for date " << date << " with result " << insert_result << std::endl;
            }
            // Verify insertion by immediately querying
            Value_t verify_ptr = date_hash->Get(date_key);
            if (verify_ptr == NONE) {
                std::cerr << "Warning: Failed to verify insertion for date " << date << std::endl;
            }
            // Note: date_key is managed by PeaHash and date_keys_map, don't free it here
        } else {
            segment_tree = it->second;
        }
        
        // Update segment tree (similar to bplus_tree->update)
        // Parse time information
        std::string time_str = row->utctimestamp;
        int interval_idx = TimeUtil::time_to_10min_interval(time_str);
        
        // Generate token (simplified, similar to b_st_bf_test.cpp)
        // In real scenario, you might want to use actual keywords from CSV data
        std::string time_key = "utctimestamp" + std::to_string(TimeUtil::date_to_timestamp(date)) + "_" + std::to_string(interval_idx);
        std::shared_ptr<std::vector<unsigned char>> token = 
            std::make_shared<std::vector<unsigned char>>(time_key.begin(), time_key.end());
        
        // For simplicity, use empty keywords vector (in real scenario, use spatial codes)
        std::vector<std::string> keywords = {}; // Can be extended with spatial codes
        
        // Update segment tree: segment_tree->update(interval_idx, token, keywords)
        segment_tree->update(interval_idx, token, keywords);
        
        row_count++;
        if (row_count % 100000 == 0) {
            std::cout << "Processed " << row_count << " rows, " << date_count << " unique dates" << std::endl;
        }
    }
    
    gettimeofday(&tv2, NULL);
    double load_duration = (double)(tv2.tv_usec - tv1.tv_usec) / 1000000 +
                          (double)(tv2.tv_sec - tv1.tv_sec);
    
    file.close();
    
    auto end_build = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> build_time = end_build - start_build;
    
    std::cout << "Index build complete." << std::endl;
    std::cout << "Build time: " << build_time.count() << " ms" << std::endl;
    
    std::cout << "\n=== Loading Statistics ===" << std::endl;
    std::cout << "Total rows processed: " << row_count << std::endl;
    std::cout << "Unique dates: " << date_count << std::endl;
    std::cout << "Loading time: " << load_duration << " seconds" << std::endl;
    std::cout << "Throughput: " << (row_count / load_duration) << " rows/second" << std::endl;
    
    // Display statistics for each date
    std::cout << "\n=== Date Statistics ===" << std::endl;
    for (const auto& pair : segment_trees_map) {
        std::cout << "Date: " << pair.first 
                  << ", SegmentTree size: " << pair.second->get_size() << std::endl;
    }
    
    // Test queries - similar to PerformSpatiotemporalQueryAndVerify in b_st_bf_test.cpp
    std::cout << "\n=== Testing Queries ===" << std::endl;
    
    if (!segment_trees_map.empty()) {
        // Test query for the example date
        std::string test_date = example_key; // "2012-04-03"
        
        // First check if this date exists in our data
        if (segment_trees_map.find(test_date) == segment_trees_map.end() && !segment_trees_map.empty()) {
            test_date = segment_trees_map.begin()->first; // Use first available date
        }
        
        std::cout << "Querying date: " << test_date << std::endl;
        
        // Query from PeaHash (similar to bplus_tree->search)
        // Use the same key object that was used during insertion
        string_key* query_key = nullptr;
        auto key_it = date_keys_map.find(test_date);
        if (key_it != date_keys_map.end()) {
            query_key = key_it->second;
        } else {
            // Fallback: create new key (but this might not work due to pointer comparison)
            query_key = createDateKey(test_date);
        }
        
        Value_t tree_ptr = date_hash->Get(query_key);
        
        if (tree_ptr != NONE) {
            // Get SegmentTree from map
            auto it = segment_trees_map.find(test_date);
            if (it != segment_trees_map.end()) {
                std::shared_ptr<SegmentTree> segment_tree = it->second;
                
                // Query morning intervals (00:00:00 - 12:00:00) = intervals 0-71
                auto query_start = std::chrono::high_resolution_clock::now();
                auto morning_tokens = segment_tree->query(0, 71);
                auto query_end = std::chrono::high_resolution_clock::now();
                auto query_time = std::chrono::duration<double, std::milli>(query_end - query_start).count();
                std::cout << "Morning intervals (0-71): " << morning_tokens.size() 
                          << " tokens, query time: " << query_time << " ms" << std::endl;
                
                // Query afternoon intervals (12:00:00 - 18:00:00) = intervals 72-107
                query_start = std::chrono::high_resolution_clock::now();
                auto afternoon_tokens = segment_tree->query(72, 107);
                query_end = std::chrono::high_resolution_clock::now();
                query_time = std::chrono::duration<double, std::milli>(query_end - query_start).count();
                std::cout << "Afternoon intervals (72-107): " << afternoon_tokens.size() 
                          << " tokens, query time: " << query_time << " ms" << std::endl;
                
                // Query evening intervals (18:00:00 - 24:00:00) = intervals 108-143
                query_start = std::chrono::high_resolution_clock::now();
                auto evening_tokens = segment_tree->query(108, 143);
                query_end = std::chrono::high_resolution_clock::now();
                query_time = std::chrono::duration<double, std::milli>(query_end - query_start).count();
                std::cout << "Evening intervals (108-143): " << evening_tokens.size() 
                          << " tokens, query time: " << query_time << " ms" << std::endl;
            }
        } else {
            std::cout << "Date " << test_date << " not found in hash table" << std::endl;
        }
        
        // Only free if we created a new key (not from date_keys_map)
        if (key_it == date_keys_map.end() && query_key != nullptr) {
            free(query_key);
        }
        
        // Test queryByTimeRange function (similar to bplus_tree->query_sql)
        std::cout << "\n=== Testing queryByTimeRange (similar to bplus_tree->query_sql) ===" << std::endl;
        if (!segment_trees_map.empty()) {
            std::string first_date = segment_trees_map.begin()->first;
            std::string query_start_time = first_date + " 01:20:00+00";
            std::string query_end_time = first_date + " 22:40:00+00";
            
            std::cout << "Query time range: " << query_start_time << " to " << query_end_time << std::endl;
            
            auto range_query_start = std::chrono::high_resolution_clock::now();
            std::vector<SegmentTree::IntervalResult> range_results = 
                queryByTimeRange(date_hash, segment_trees_map, date_keys_map, query_start_time, query_end_time);
            auto range_query_end = std::chrono::high_resolution_clock::now();
            auto range_query_time = std::chrono::duration<double, std::milli>(range_query_end - range_query_start).count();
            
            size_t total_tokens = 0;
            for (const auto& ir : range_results) {
                total_tokens += ir.tokens.size();
            }
            std::cout << "Range query returned: " << range_results.size() << " intervals, " 
                      << total_tokens << " tokens, query time: " << range_query_time << " ms" << std::endl;
        }
    }
    
    // Cleanup
    std::cout << "\n=== Cleanup ===" << std::endl;
    for (auto* row : all_rows) {
        delete row;
    }
    // Note: date_keys_map keys are managed by PeaHash, don't free them here
    // segment_trees_map will automatically clean up shared_ptr<SegmentTree>
    delete date_hash;
    
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
}

bool check_ratio() {
    int read_portion = (int) (read_ratio * 100);
    int insert_portion = (int) (insert_ratio * 100);
    int delete_portion = (int) (delete_ratio * 100);
    if ((read_portion + insert_portion + delete_portion) != 100) return false;
    return true;
}

int main(int argc, char *argv[]) {
    set_affinity(0);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    initCap = FLAGS_i;
    thread_num = FLAGS_t;
    load_num = FLAGS_n;
    operation_num = FLAGS_p;
    key_type = FLAGS_k;
    index_type = FLAGS_index;
    distribution = FLAGS_distribution;
    load_type = FLAGS_loadType;
    std::cout << "Distribution = " << distribution << std::endl;
    std::string fixed("fixed");
    operation = FLAGS_op;
    open_epoch = FLAGS_e;
    EPOCH_DURATION = FLAGS_ed;
    msec = FLAGS_ms;
    var_length = FLAGS_vl;
    pool_size = FLAGS_ps * 1024ul * 1024ul * 1024ul; /*pool_size*/
    if (open_epoch == true)
        std::cout << "EPOCH registration in application level" << std::endl;

    read_ratio = FLAGS_r;
    insert_ratio = FLAGS_s;
    delete_ratio = FLAGS_d;
    skew_factor = FLAGS_skew;
    if (distribution == "skew")
        std::cout << "Skew theta = " << skew_factor << std::endl;

    // if (operation == "mixed") {
    //   std::cout << "Search ratio = " << read_ratio << std::endl;
    //   std::cout << "Insert ratio = " << insert_ratio << std::endl;
    //   std::cout << "Delete ratio = " << delete_ratio << std::endl;
    // }

    if (!check_ratio()) {
        std::cout << "The ratio is wrong!" << std::endl;
        return 0;
    }

    // Special operation: date-based hash table test
    if (operation == "date-hash") {
        testDateBasedHashTable();
        return 0;
    }

    if (key_type.compare(fixed) == 0) {
        Run<uint64_t>();
    } else {
        std::cout << "Variable-length key = " << var_length << std::endl;
        Run<string_key *>();
    }
}
