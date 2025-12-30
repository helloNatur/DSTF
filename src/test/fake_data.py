import random
from datetime import datetime, timedelta
import csv
import uuid
# 前30万数据集的最大值和最小值分别为
#        min_utc_ts       |       max_utc_ts       
# ------------------------+------------------------
#  2012-04-03 18:17:18+00 | 2012-07-30 04:05:37+00

#    lat_min   |  lat_max  |   lon_min   |   lon_max   
# -------------+-----------+-------------+-------------
#  35.51076909 | 35.866106 | 139.4708776 | 139.9111032
# 生成均匀分布的时间戳
def generate_timestamp(start, end, num_records):
    start = start.replace("+00", "+0000")  # 修正时区格式
    end = end.replace("+00", "+0000")
    start_ts = datetime.strptime(start, "%Y-%m-%d %H:%M:%S%z")
    end_ts = datetime.strptime(end, "%Y-%m-%d %H:%M:%S%z")
    delta = (end_ts - start_ts).total_seconds()
    return [start_ts + timedelta(seconds=random.uniform(0, delta)) for _ in range(num_records)]

# 生成随机的venueid和venuecategoryid（16位十六进制）
def generate_hex_id():
    return uuid.uuid4().hex[:16]

# 常见venuecategory列表（从样本提取）
venue_categories = [
    "Arts & Crafts Store", "Bridge", "Home (private)", "Medical Center", "Food Truck",
    "Food & Drink Shop", "Coffee Shop", "Bus Station", "Bank", "Gastropub",
    "Electronics Store", "Mobile Phone Shop", "Café", "Automotive Shop", "Drugstore / Pharmacy",
    "Historic Site", "Ice Cream Shop", "Bar", "Burger Joint", "Comedy Club",
    "American Restaurant", "Post Office", "Fried Chicken Joint", "Library", "Beer Garden",
    "Concert Hall"
]
# 对应的venuecategoryid（部分从样本提取，部分随机生成）
venue_category_ids = {
    cat: generate_hex_id() if cat not in [
        "Arts & Crafts Store", "Bridge", "Home (private)", "Medical Center", "Food Truck",
        "Food & Drink Shop", "Coffee Shop", "Bus Station", "Bank", "Gastropub",
        "Electronics Store", "Mobile Phone Shop", "Café", "Automotive Shop"
    ] else {
        "Arts & Crafts Store": "4bf58dd8d48988d127951735",
        "Bridge": "4bf58dd8d48988d1df941735",
        "Home (private)": "4bf58dd8d48988d103941735",
        "Medical Center": "4bf58dd8d48988d104941735",
        "Food Truck": "4bf58dd8d48988d1cb941735",
        "Food & Drink Shop": "4bf58dd8d48988d118951735",
        "Coffee Shop": "4bf58dd8d48988d1e0931735",
        "Bus Station": "4bf58dd8d48988d12b951735",
        "Bank": "4bf58dd8d48988d10a951735",
        "Gastropub": "4bf58dd8d48988d155941735",
        "Electronics Store": "4bf58dd8d48988d122951735",
        "Mobile Phone Shop": "4f04afc02fb6e1c99f3db0bc",
        "Café": "4bf58dd8d48988d16d941735",
        "Automotive Shop": "4bf58dd8d48988d124951735"
    }[cat]
    for cat in venue_categories
}

# 数据集参数
n_total = 300000  #  控制 符合时间范围 记录数的参数

lat_total = (35.51076909, 35.866106)    # 总纬度范围
lon_total = (139.4708776, 139.9111032)  # 总经度范围
n_userid818 = 4350  # t2中userid=818的记录数
start_time = "2012-04-03 18:17:18+0000"  # 修正为+0000
end_time = "2012-07-30 04:05:37+0000"    # 修正为+0000
timezone_offset = -240  # 固定为-240分钟

# 生成t1数据
t1_records = []
timestamps = generate_timestamp(start_time, end_time, n_total)
# 先生成记录，稍后排序
temp_records = []
for i in range(n_total):
    # id = str(i + 1)
    venuecategory = random.choice(venue_categories)
    if i < n_query:
        # 查询范围内记录
        lat = random.uniform(*lat_query)
        lon = random.uniform(*lon_query)
        userid = "818"  # 确保join条件
    else:
        # 非查询范围，远离查询边界
        lat = random.uniform(40.5, 40.68) if random.random() < 0.5 else random.uniform(40.78, 41.0)
        lon = random.uniform(-74.2, -74.02) if random.random() < 0.5 else random.uniform(-73.96, -73.7)
        userid = str(random.randint(1, 1051))  # userid范围参考样本
    timestamp = timestamps[i]
    temp_records.append({
        'userid': userid,
        'venuecategory': venuecategory,
        'lat': lat,
        'lon': lon,
        'timestamp': timestamp
    })

# 按timestamp排序
temp_records.sort(key=lambda x: x['timestamp'])

# 分配id并生成最终t1记录
for i, record in enumerate(temp_records):
    id = str(i + 1)  # id按时间排序后从1递增
    timestamp = record['timestamp'].strftime("%Y-%m-%d %H:%M:%S%z")
    t1_records.append([
        record['userid'],
        generate_hex_id(),  # venueid
        venue_category_ids[record['venuecategory']],  # venuecategoryid
        record['venuecategory'],
        f"{record['lat']:.8f}",  # 保持高精度
        f"{record['lon']:.8f}",
        str(timezone_offset),
        timestamp,
        id
    ])

# 生成t2数据
t2_records = []
for i in range(n_total):
    id = str(i + 1)
    venuecategory = random.choice(venue_categories)
    lat = random.uniform(*lat_total)
    lon = random.uniform(*lon_total)
    timestamp = timestamps[i].strftime("%Y-%m-%d %H:%M:%S%z")
    userid = "818" if i < n_userid818 else str(random.randint(1, 1051))
    t2_records.append([
        userid,
        generate_hex_id(),
        venue_category_ids[venuecategory],
        venuecategory,
        f"{lat:.8f}",
        f"{lon:.8f}",
        str(timezone_offset),
        timestamp,
        id
    ])

# 按timestamp排序t2记录
t2_records.sort(key=lambda x: datetime.strptime(x[7], "%Y-%m-%d %H:%M:%S%z"))
# 重新分配id以保持顺序
for i, record in enumerate(t2_records):
    record[8] = str(i + 1)

# 保存CSV文件
with open("/nvme/baum/git-project/JXT2/data/table1/table1_k7_j1_200000.csv", "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["userid", "venueid", "venuecategoryid", "venuecategory", "latitude", "longitude", "timezoneoffset", "utctimestamp", "id"])
    writer.writerows(t1_records)

with open("/nvme/baum/git-project/JXT2/data/table2/table2_k7_j1_200000.csv", "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["userid", "venueid", "venuecategoryid", "venuecategory", "latitude", "longitude", "timezoneoffset", "utctimestamp", "id"])
    writer.writerows(t2_records)

print("Generated CSV files: table1_k7_j1_227428.csv, table2_k7_j1_227428.csv")