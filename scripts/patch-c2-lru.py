#!/usr/bin/env python3
"""v1.45.0 C2 LRU cache patch for graph_store.cpp."""
import sys
pc = 'D:/Data Kerja/Personal/AI/icm-graph/src/graph/graph_store.cpp'
with open(pc, 'rb') as f:
    s = f.read()

# 1) Clear: also wipe iter map
old1 = b'    node_cache_order_.clear();\r\n}'
new1 = b'    node_cache_order_.clear();\r\n    node_cache_iters_.clear();\r\n}'
ok1 = (s.count(old1) == 1)
if ok1:
    s = s.replace(old1, new1, 1)

# 2) cacheGetNode: LRU touch on hit
old2 = (b'    if (it == node_cache_.end()) return std::nullopt;\r\n'
        b'    return it->second;\r\n}')
new2 = (b'    if (it == node_cache_.end()) return std::nullopt;\r\n'
        b'    // v1.45.0 C2: LRU touch -- splice key to back via stored iter.\r\n'
        b'    auto iit = node_cache_iters_.find(path);\r\n'
        b'    if (iit != node_cache_iters_.end()) {\r\n'
        b'        node_cache_order_.splice(node_cache_order_.end(), node_cache_order_, iit->second);\r\n'
        b'    }\r\n'
        b'    return it->second;\r\n}')
ok2 = (s.count(old2) >= 1)
if ok2:
    s = s.replace(old2, new2, 1)

# 3) cachePutNode: list-based evict + iter tracking
old3 = (b'    if (node_cache_.size() >= NODE_CACHE_MAX && !node_cache_order_.empty()) {\r\n'
        b'        node_cache_.erase(node_cache_order_.front());\r\n'
        b'        node_cache_order_.erase(node_cache_order_.begin());\r\n'
        b'    }\r\n'
        b'    auto [it, inserted] = node_cache_.emplace(path, node);\r\n'
        b'    if (inserted) node_cache_order_.push_back(path);\r\n'
        b'    else it->second = node;  // refresh; order unchanged\r\n')
new3 = (b'    // v1.45.0 C2: LRU eviction via std::list + iter map. O(1) pop_front.\r\n'
        b'    if (node_cache_.size() >= NODE_CACHE_MAX && !node_cache_order_.empty()) {\r\n'
        b'        const std::string victim = node_cache_order_.front();\r\n'
        b'        node_cache_.erase(victim);\r\n'
        b'        node_cache_iters_.erase(victim);\r\n'
        b'        node_cache_order_.pop_front();\r\n'
        b'    }\r\n'
        b'    auto [it, inserted] = node_cache_.emplace(path, node);\r\n'
        b'    if (inserted) {\r\n'
        b'        node_cache_order_.push_back(path);\r\n'
        b'        node_cache_iters_[path] = std::prev(node_cache_order_.end());\r\n'
        b'    } else {\r\n'
        b'        it->second = node;\r\n'
        b'        auto iit = node_cache_iters_.find(path);\r\n'
        b'        if (iit != node_cache_iters_.end()) {\r\n'
        b'            node_cache_order_.splice(node_cache_order_.end(), node_cache_order_, iit->second);\r\n'
        b'        }\r\n'
        b'    }\r\n')
ok3 = (s.count(old3) == 1)
if ok3:
    s = s.replace(old3, new3, 1)

open(pc, 'wb').write(s)
print(f'patched clear={ok1} get={ok2} put={ok3}')
