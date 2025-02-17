// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Wang,Yao(wangyao02@baidu.com)
//          Zhangyi Chen(chenzhangyi01@baidu.com)
//          Ge,Jun(gejun@baidu.com)

#ifndef BRAFT_RAFT_CONFIGURATION_H
#define BRAFT_RAFT_CONFIGURATION_H

#include <string>
#include <ostream>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <butil/strings/string_piece.h>
#include <butil/endpoint.h>
#include <butil/logging.h>

namespace braft {

typedef std::string GroupId;
// GroupId with version, format: {group_id}_{index}
typedef std::string VersionedGroupId;

extern const char *DEFAULT_PREFER_ZONE;
extern const char *DEFAULT_CURRENT_ZONE;

enum Role {
    REPLICA = 0,
    WITNESS = 1,
};

struct HostNameAddr {
    HostNameAddr() : hostname(""), port(0) {}
    explicit HostNameAddr(const std::string& hostname_, uint16_t port_) : hostname(hostname_), port(port_) {}

    HostNameAddr(const HostNameAddr& rhs) = default;
    HostNameAddr(HostNameAddr&& rhs) = default;
    HostNameAddr& operator=(const HostNameAddr& addr) = default;
    HostNameAddr& operator=(HostNameAddr&& addr)  noexcept {
        if(&addr == this) {
            return *this;
        }
        hostname = std::move(addr.hostname);
        port = addr.port;
        return *this;
    }

    void reset() {
        hostname.clear();
        port = 0;
    }

    std::string to_string() const;

    std::string hostname;
    uint16_t port;
};

inline bool operator<(const HostNameAddr& addr1, const HostNameAddr& addr2) {
    return (addr1.hostname != addr2.hostname) ? (addr1.hostname < addr2.hostname) : (addr1.port < addr2.port);
}

inline bool operator==(const HostNameAddr& addr1, const HostNameAddr& addr2) {
    return addr1.hostname == addr2.hostname && addr1.port == addr2.port;
}

inline bool operator!=(const HostNameAddr& addr1, const HostNameAddr& addr2) {
    return !(addr1 == addr2);
}

inline std::ostream& operator<<(std::ostream& os, const HostNameAddr& addr) {
    return os << addr.hostname << ":" << addr.port;
}

inline std::string HostNameAddr::to_string() const {
        std::ostringstream oss;
        oss << *this;
        return oss.str();
}


// Represent a participant in a replicating group.
struct PeerId {
    butil::EndPoint addr; // ip+port.
    int idx; // idx in same addr, default 0
    Role role = REPLICA;
    HostNameAddr hostname_addr; // hostname+port.
    enum class Type {
        EndPoint = 0,
        HostName
    };
    Type type_;
    std::string prefer_zone{DEFAULT_PREFER_ZONE};
    std::string current_zone{DEFAULT_CURRENT_ZONE};

    PeerId() : idx(0), role(REPLICA), type_(Type::EndPoint) {}
    explicit PeerId(butil::EndPoint addr_) : addr(addr_), idx(0), role(REPLICA), type_(Type::EndPoint) {}
    PeerId(butil::EndPoint addr_, int idx_) : addr(addr_), idx(idx_), role(REPLICA), type_(Type::EndPoint) {}
    PeerId(butil::EndPoint addr_, int idx_, bool witness) : addr(addr_), idx(idx_), type_(Type::EndPoint) {
        if (witness) {
            this->role = WITNESS;
        }    
    }
    /*intended implicit*/PeerId(const std::string& str) 
    { CHECK_EQ(0, parse(str)); }

    PeerId(const PeerId& id) = default;
    PeerId(PeerId&& id) = default;
    PeerId& operator=(const PeerId& id) = default;
    PeerId& operator=(PeerId&& id)  noexcept {
        if ( &id == this) {
            return *this;
        }
        addr = std::move(id.addr);
        idx = std::move(id.idx);
        hostname_addr = std::move(id.hostname_addr);
        type_ = std::move(id.type_);
        role = std::move(id.role);
        prefer_zone = std::move(id.prefer_zone);
        current_zone = std::move(id.current_zone);

        return *this;
    }

    void reset() {
        if (type_ == Type::EndPoint) {
            addr.ip = butil::IP_ANY;
            addr.port = 0;
        } 
        else {
            hostname_addr.reset();
        }
        idx = 0;
        role = REPLICA;
    }

    bool is_empty() const {
        if (type_ == Type::EndPoint) {
            return (addr.ip == butil::IP_ANY && addr.port == 0 && idx == 0);
        } else {
            return (hostname_addr.hostname.empty() && hostname_addr.port == 0 && idx == 0);
        }
    }
    bool is_witness() const {
        return role == WITNESS;
    }
    int parse(const std::string& str) {
        reset();
        char temp_str[256]; // max length of DNS Name < 255
        int value = REPLICA;
        int port;
        char prefer_zone_str[32];
        char current_zone_str[32];
        if (str.empty()) {
            return -1;
        }
        // cloud availability zone info could be parsed if user has specified
        uint16_t colon_count = std::count(str.begin(), str.end(), ':');
        if (colon_count < 4) {
            // conf format, 172-17-0-1.default.pod.cluster.local:8002:0:0
            if (2 > sscanf(str.c_str(), "%[^:]%*[:]%d%*[:]%d%*[:]%d", temp_str, &port, &idx, &value)) {
                reset();
                return -1;
            }
        } else {
            // conf format, 172-17-0-1.default.pod.cluster.local:8002:0:prefer_zone:current_zone:0
            if (2 > sscanf(str.c_str(), "%[^:]%*[:]%d%*[:]%d:%[^:]:%[^:]%*[:]%d", temp_str, &port, &idx, prefer_zone_str, current_zone_str, &value)) {
                reset();
                return -1;
            }
            prefer_zone.assign(prefer_zone_str);
            current_zone.assign(current_zone_str);
        }
        role = (Role)value;
        if (role > WITNESS) {
            reset();
            return -1;
        }
        if (0 != butil::str2ip(temp_str, &addr.ip)) {
            type_ = Type::HostName;
            hostname_addr.hostname = temp_str;
            hostname_addr.port = port;
        } else {
            type_ = Type::EndPoint;
            addr.port = port;
        }
        return 0;
    }

    // format, hostname:port:idx:prefer_zone:current_zone:role
    // 172-17-0-1.default.pod.cluster.local:8002:0:ap-northeast-1a:ap-northeast-1c:0
    std::string to_string() const {
        char str[512]; // max length of DNS Name < 255
        if (type_ == Type::EndPoint) {
            if (prefer_zone == DEFAULT_PREFER_ZONE && current_zone == DEFAULT_CURRENT_ZONE) {
                snprintf(str, sizeof(str), "%s:%d:%d", butil::endpoint2str(addr).c_str(), idx, int(role));
            } else {
                snprintf(str, sizeof(str), "%s:%d:%s:%s:%d", butil::endpoint2str(addr).c_str(), idx, prefer_zone.c_str(), current_zone.c_str(), int(role));
            }
        } else {
            if (prefer_zone == DEFAULT_PREFER_ZONE && current_zone == DEFAULT_CURRENT_ZONE) {
                snprintf(str, sizeof(str), "%s:%d:%d", hostname_addr.to_string().c_str(), idx, int(role));
            } else {
                snprintf(str, sizeof(str), "%s:%d:%s:%s:%d", hostname_addr.to_string().c_str(), idx,  prefer_zone.c_str(), current_zone.c_str(), int(role));
            }
        }
        return std::string(str);
    }
    
};

inline bool operator<(const PeerId& id1, const PeerId& id2) {
    if (id1.type_ != id2.type_) {
        LOG(WARNING) << "PeerId id1 and PeerId id2 do not have same type(IP Addr or Hostname).";
        if (id1.type_ == PeerId::Type::EndPoint) {
            if (strcmp(butil::endpoint2str(id1.addr).c_str(), id2.hostname_addr.to_string().c_str()) < 0) {
                return true;
            } else {
                return false;
            }
        } else {
            if (strcmp(id1.hostname_addr.to_string().c_str(), butil::endpoint2str(id2.addr).c_str()) < 0) {
                return true;
            } else {
                return false;
            }
        }
    } else {
        if (id1.type_ == PeerId::Type::EndPoint) {
            if (id1.addr < id2.addr) {
                return true;
            } else {
                return id1.addr == id2.addr && id1.idx < id2.idx;
            }
        } else {
            if (id1.hostname_addr < id2.hostname_addr) {
                return true;
            } else {
                return id1.hostname_addr == id2.hostname_addr && id1.idx < id2.idx;
            }
        }
    }
}

inline bool operator==(const PeerId& id1, const PeerId& id2) {
    if (id1.type_ != id2.type_) {
        return false;
    }
    if (id1.type_ == PeerId::Type::EndPoint) {
        return (id1.addr == id2.addr && id1.idx == id2.idx);
    } else {
        return (id1.hostname_addr == id2.hostname_addr && id1.idx == id2.idx);
    }
}

inline bool operator!=(const PeerId& id1, const PeerId& id2) {
    return !(id1 == id2);
}

inline std::ostream& operator << (std::ostream& os, const PeerId& id) {
    if (id.type_ == PeerId::Type::EndPoint) {
        if (id.prefer_zone == DEFAULT_PREFER_ZONE && id.current_zone == DEFAULT_CURRENT_ZONE) {
            return os << id.addr << ':' << id.idx << ':' << int(id.role);
        } else {
            return os << id.addr << ':' << id.idx << ':' << id.prefer_zone << ':' << id.current_zone << ':' << int(id.role);
        }
    } else {
        if (id.prefer_zone == DEFAULT_PREFER_ZONE && id.current_zone == DEFAULT_CURRENT_ZONE) {
            return os << id.hostname_addr << ':' << id.idx << ':' << int(id.role);
        } else {
            return os << id.hostname_addr << ':' << id.idx << ':' << id.prefer_zone << ':' << id.current_zone << ':' << int(id.role);
        }
    }
}

struct NodeId {
    GroupId group_id;
    PeerId peer_id;

    NodeId(const GroupId& group_id_, const PeerId& peer_id_)
        : group_id(group_id_), peer_id(peer_id_) {
    }
    std::string to_string() const;
};

inline bool operator<(const NodeId& id1, const NodeId& id2) {
    const int rc = id1.group_id.compare(id2.group_id);
    if (rc < 0) {
        return true;
    } else {
        return rc == 0 && id1.peer_id < id2.peer_id;
    }
}

inline bool operator==(const NodeId& id1, const NodeId& id2) {
    return (id1.group_id == id2.group_id && id1.peer_id == id2.peer_id);
}

inline bool operator!=(const NodeId& id1, const NodeId& id2) {
    return (id1.group_id != id2.group_id || id1.peer_id != id2.peer_id);
}

inline std::ostream& operator << (std::ostream& os, const NodeId& id) {
    return os << id.group_id << ':' << id.peer_id;
}

inline std::string NodeId::to_string() const {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
}

// A set of peers.
class Configuration {
public:
    typedef std::set<PeerId>::const_iterator const_iterator;
    // Construct an empty configuration.
    Configuration() {}

    // Construct from peers stored in std::vector.
    explicit Configuration(const std::vector<PeerId>& peers) {
        for (size_t i = 0; i < peers.size(); i++) {
            _peers.insert(peers[i]);
        }
    }

    // Construct from peers stored in std::set
    explicit Configuration(const std::set<PeerId>& peers) : _peers(peers) {}

    // Assign from peers stored in std::vector
    void operator=(const std::vector<PeerId>& peers) {
        _peers.clear();
        for (size_t i = 0; i < peers.size(); i++) {
            _peers.insert(peers[i]);
        }
    }

    // Assign from peers stored in std::set
    void operator=(const std::set<PeerId>& peers) {
        _peers = peers;
    }

    // Remove all peers.
    void reset() { _peers.clear(); }

    bool empty() const { return _peers.empty(); }
    size_t size() const { return _peers.size(); }

    const_iterator begin() const { return _peers.begin(); }
    const_iterator end() const { return _peers.end(); }

    // Clear the container and put peers in. 
    void list_peers(std::set<PeerId>* peers) const {
        peers->clear();
        *peers = _peers;
    }
    void list_peers(std::vector<PeerId>* peers) const {
        peers->clear();
        peers->reserve(_peers.size());
        std::set<PeerId>::iterator it;
        for (it = _peers.begin(); it != _peers.end(); ++it) {
            peers->push_back(*it);
        }
    }

    void append_peers(std::set<PeerId>* peers) {
        peers->insert(_peers.begin(), _peers.end());
    }

    // Add a peer.
    // Returns true if the peer is newly added.
    bool add_peer(const PeerId& peer) {
        return _peers.insert(peer).second;
    }

    // Remove a peer.
    // Returns true if the peer is removed.
    bool remove_peer(const PeerId& peer) {
        return _peers.erase(peer);
    }

    // True if the peer exists.
    bool contains(const PeerId& peer_id) const {
        return _peers.find(peer_id) != _peers.end();
    }

    // True if ALL peers exist.
    bool contains(const std::vector<PeerId>& peers) const {
        for (size_t i = 0; i < peers.size(); i++) {
            if (_peers.find(peers[i]) == _peers.end()) {
                return false;
            }
        }
        return true;
    }

    // True if peers are same.
    bool equals(const std::vector<PeerId>& peers) const {
        std::set<PeerId> peer_set;
        for (size_t i = 0; i < peers.size(); i++) {
            if (_peers.find(peers[i]) == _peers.end()) {
                return false;
            }
            peer_set.insert(peers[i]);
        }
        return peer_set.size() == _peers.size();
    }

    bool equals(const Configuration& rhs) const {
        if (size() != rhs.size()) {
            return false;
        }
        // The cost of the following routine is O(nlogn), which is not the best
        // approach.
        for (const_iterator iter = begin(); iter != end(); ++iter) {
            if (!rhs.contains(*iter)) {
                return false;
            }
        }
        return true;
    }
    
    // Get the difference between |*this| and |rhs|
    // |included| would be assigned to |*this| - |rhs|
    // |excluded| would be assigned to |rhs| - |*this|
    void diffs(const Configuration& rhs,
               Configuration* included,
               Configuration* excluded) const {
        *included = *this;
        *excluded = rhs;
        for (std::set<PeerId>::const_iterator 
                iter = _peers.begin(); iter != _peers.end(); ++iter) {
            excluded->_peers.erase(*iter);
        }
        for (std::set<PeerId>::const_iterator 
                iter = rhs._peers.begin(); iter != rhs._peers.end(); ++iter) {
            included->_peers.erase(*iter);
        }
    }

    // Parse Configuration from a string into |this|
    // Returns 0 on success, -1 otherwise
    int parse_from(butil::StringPiece conf);
    
private:
    std::set<PeerId> _peers;

};

std::ostream& operator<<(std::ostream& os, const Configuration& a);

}  //  namespace braft

#endif //~BRAFT_RAFT_CONFIGURATION_H
