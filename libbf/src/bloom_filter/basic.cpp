#include <bf/bloom_filter/basic.hpp>

#include <cassert>
#include <cmath>

namespace bf {

size_t basic_bloom_filter::m(double fp, size_t capacity) {//fp:误判率，capacity:预期容量,m:输出为建立的位图长度
  auto ln2 = std::log(2);
  return std::ceil(-(capacity * std::log(fp) / ln2 / ln2));
}

size_t basic_bloom_filter::k(size_t cells, size_t capacity) {//计算最优hash函数个数：k=m/n*ln2
  auto frac = static_cast<double>(cells) / static_cast<double>(capacity);
  return std::ceil(frac * std::log(2));
}

basic_bloom_filter::basic_bloom_filter(hasher h, size_t cells, bool partition,size_t num_hash_functions)//构造函数1:自定义hash器和位图大小
    : hasher_(std::move(h)), bits_(cells), partition_(partition), num_hash_functions_(num_hash_functions) {
}

// 拷贝构造函数：完全复制 Bloom Filter 状态
// basic.cpp 中
// basic_bloom_filter::basic_bloom_filter(const basic_bloom_filter& other)
//     : hasher_(other.hasher_), bits_(other.bits_), partition_(other.partition_) {}
// // 该构造函数用于深拷贝，确保 hasher 和 bits 的状态被正确复制
// // 以防止在使用 Bloom Filter 时出现意外的状态共享问题。


basic_bloom_filter::basic_bloom_filter(double fp, size_t capacity, size_t seed,
                                       bool double_hashing, bool partition)//构造函数2:误判率，预期容量，随机种子，是否双hash，是否分区
    : partition_(partition) {
  //验证参数
  if(fp<=0.0||fp>=1.0){
    throw std::invalid_argument("False positive rate must be in (0,1)");
  }
  if (capacity == 0) {
    throw std::invalid_argument("Capacity must be greater than 0");
  }
  auto required_cells = m(fp, capacity);
  auto optimal_k = k(required_cells, capacity);
  if (partition_)
    required_cells += optimal_k - required_cells % optimal_k;
  bits_.resize(required_cells);
  hasher_ = make_hasher(optimal_k, seed, double_hashing);
}

basic_bloom_filter::basic_bloom_filter(hasher h, bitvector b,size_t num_hash_functions)
    : hasher_(std::move(h)), bits_(std::move(b)),num_hash_functions_(std::move(num_hash_functions)) {
}

basic_bloom_filter::basic_bloom_filter(basic_bloom_filter&& other)
    : hasher_(std::move(other.hasher_)), bits_(std::move(other.bits_)) {
}

void basic_bloom_filter::add(object const& o) {//插入一个元素
  auto digests = hasher_(o);
  if (partition_) {
    assert(bits_.size() % digests.size() == 0);
    auto parts = bits_.size() / digests.size();
    for (size_t i = 0; i < digests.size(); ++i)
      bits_.set(i * parts + (digests[i] % parts));
  } else {
    for (auto d : digests)
      bits_.set(d % bits_.size());
  }
}

size_t basic_bloom_filter::lookup(object const& o) const {//查询元素是否可能存在（1：可能存在；0：必然不存在）
  auto digests = hasher_(o);
  if (partition_) {
    assert(bits_.size() % digests.size() == 0);
    auto parts = bits_.size() / digests.size();
    for (size_t i = 0; i < digests.size(); ++i)
      if (!bits_[i * parts + (digests[i] % parts)])
        return 0;
  } else {
    for (auto d : digests)
      if (!bits_[d % bits_.size()])
        return 0;
  }

  return 1;
}

void basic_bloom_filter::clear() {//清空整个 Bloom filter
  bits_.reset();
}

void basic_bloom_filter::remove(object const& o) {
  for (auto d : hasher_(o))
    bits_.reset(d % bits_.size());
}

void basic_bloom_filter::swap(basic_bloom_filter& other) {
  using std::swap;
  swap(hasher_, other.hasher_);
  swap(bits_, other.bits_);
}

bitvector const& basic_bloom_filter::storage() const {
  return bits_;
}
hasher const& basic_bloom_filter::hasher_function() const {
  return hasher_;
}

void basic_bloom_filter::set_storage(bitvector b) {
  if(partition_ && b.size() % num_hash_functions_ != 0) {
    throw std::invalid_argument("Bitvector size must be a multiple of the number of hash functions when partitioning is enabled");
  }
  // if (b.size() < bits_.size()) {
  //   throw std::invalid_argument("New bitvector size must not be smaller than current size");
  // }
  if (b.size() != bits_.size()) {
    throw std::invalid_argument("Bitvector size is incompatible with the Bloom filter");
  }
  bits_ = std::move(b);
}

} // namespace bf
