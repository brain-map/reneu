#pragma once

#include <boost/pending/disjoint_sets.hpp>
#include "../type_aliase.hpp"
#include "./utils.hpp"

// #include <boost/serialization/serialization.hpp>
// #include <boost/serialization/map.hpp>


namespace reneu{

class DisjointSets{

private:
using Rank_t = std::map<segid_t, size_t>;
using Parent_t = std::map<segid_t, segid_t>;
using PropMapRank_t = boost::associative_property_map<Rank_t>;
using PropMapParent_t = boost::associative_property_map<Parent_t>;
using BoostDisjointSets = boost::disjoint_sets<PropMapRank_t, PropMapParent_t>; 

Rank_t _mapRank;
Parent_t _mapParent;
PropMapRank_t _propMapRank;
PropMapParent_t _propMapParent;
BoostDisjointSets _dsets;

public:
DisjointSets():
    _propMapRank(_mapRank), _propMapParent(_mapParent),
    _dsets(_propMapRank, _propMapParent){}


DisjointSets(const Segmentation& seg):
        _propMapRank(_mapRank), _propMapParent(_mapParent),
        _dsets(_propMapRank, _propMapParent){
    
    auto segids = get_nonzero_segids(seg);
    for(const auto& segid : segids){
        _dsets.make_set(segid);
    }
}

// friend class boost::serialization::access;
// template<class Archive>
// void serialize(Archive ar, const unsigned int version){
//     ar & _mapRank;
//     ar & _mapParent;
//     ar & _propMapRank;
//     ar & _propMapParent;
//     ar & _dsets;
// }

void make_set(const segid_t& segid ){
    _dsets.make_set(segid);
}

void union_set(segid_t s0, segid_t s1){
    _dsets.union_set(s0, s1);
}

segid_t find_set(segid_t sid){
    const auto& root = _dsets.find_set(sid);
    if(root == 0)
        return sid;
    else
        return root;
}

auto relabel(Segmentation&& seg){
    auto segids = get_nonzero_segids(seg);
    // Flatten the parents tree so that the parent of every element is its representative.
    _dsets.compress_sets(segids.begin(), segids.end());
    std::cout<< "get "<< 
                _dsets.count_sets(segids.begin(), segids.end()) << 
                " final objects."<< std::endl;

    std::cout<< "relabel the fragments to a flat segmentation." << std::endl;
    const auto& [sz, sy, sx] = seg.shape();
    for(std::size_t z=0; z<sz; z++){
        for(std::size_t y=0; y<sy; y++){
            for(std::size_t x=0; x<sx; x++){
                const auto& sid = seg(z,y,x);
                if(sid > 0){
                    const auto& rootID = find_set(sid);
                    if(sid!=rootID){
                        assert(rootID > 0);
                        seg(z,y,x) = rootID;
                    }
                }
            }
        }
    }


    // this implementation will mask out all the objects that is not in the set!
    // We should not do it in the global materialization stage.
    // std::transform(seg.begin(), seg.end(), seg.begin(), 
    //    [this](segid_t segid)->segid_t{return this->find_set(segid);}
    // );
    return seg;
}

inline auto py_relabel(PySegmentation& pyseg){
    return relabel(std::move(pyseg));
}

};

} // namespace reneu