#pragma once

#include <assert.h>
#include <iostream>
#include <stack>
#include <boost/pending/disjoint_sets.hpp>
#include "../type_aliase.hpp"
#include "./utils.hpp"

// #include <boost/serialization/serialization.hpp>
// #include <boost/serialization/map.hpp>


namespace reneu{

using SegPairs = std::vector<std::pair<segid_t, segid_t>>;
auto seg_pairs_to_array(SegPairs& pairs){
    // std::cout<<"convert to array."<<std::endl;
    const auto& pairNum = pairs.size();
    xt::xtensor<segid_t, 2>::shape_type sh = {pairNum, 2};
    auto arr = xt::empty<segid_t>(sh);
    for(std::size_t idx=0; idx<pairNum; idx++){
        const auto& [segid0, root] = pairs[idx];
        arr(idx, 0) = segid0;
        arr(idx, 1) = root;
        // std::cout<<"merge "<<segid0<<", "<<root<<std::endl;
    }
    return arr;
}

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
    // To-Do: use contain function in C++20
    const auto& search = _mapRank.find(segid);
    if(search == _mapRank.end())
        _dsets.make_set(segid);
}

void union_set(const segid_t s0, const segid_t s1){
    _dsets.union_set(s0, s1);
}

void make_and_union_set(const segid_t s0, const segid_t s1){
    make_set(s0);
    make_set(s1);
    union_set(s0, s1);
}

segid_t find_set(segid_t sid){
    const auto& root = _dsets.find_set(sid);
    if(root == 0)
        return sid;
    else
        return root;
}

auto merge_array(const xt::xtensor<segid_t, 2>& arr){
    std::set<std::pair<segid_t, segid_t>> pairs = {};
    assert(arr.shape(1) == 2);

    // in case there exist a lot of duplicates in this array
    // we make a small set first to make it more efficient
    for(std::size_t idx=0; idx<arr.shape(0); idx++){
        const auto& segid0 = arr(idx, 0);
        const auto& segid1 = arr(idx, 1);
        // const auto& pair = std::make_tuple(segid0, segid1);
        pairs.emplace(segid0, segid1);
    }

    for(const auto& [segid0, segid1] : pairs){
        make_and_union_set(segid0, segid1);
    }
}

inline auto py_merge_array(xt::pytensor<segid_t, 2>& pyarr){
    return merge_array(std::move(pyarr));
}



auto to_array(){
    // std::cout<<"find the root for each segment."<<std::endl;
    SegPairs pairs = {};
    for(const auto& [segid0, parent]: _mapParent){
        const auto& root = find_set(segid0);
        if(root != segid0){
            // std::cout<<"add pair to stack: "<<segid0<<", "<< root<<std::endl;
            pairs.emplace_back(segid0, root);
        }
    }

    const auto& arr = seg_pairs_to_array(pairs);
    return arr;
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

}; // class of DisjointSets

/**
 * @brief given a fragment array and plain segmentation, find the disjoint sets. 
 * 
 * @param frag The fragment segmentation containing super voxels.
 * @param seg The plain segmentation generated by agglomerating framgents
 * @return The corresponding object pairs that need to be merged 
 */
auto agglomerated_segmentation_to_merge_pairs(const PySegmentation& frag, const PySegmentation& seg){
    assert(frag.shape(0) == seg.shape(0));
    assert(frag.shape(1) == seg.shape(1));
    assert(frag.shape(2) == seg.shape(2));

    SegPairs pairs = {};
    // auto dsets = DisjointSets();
    // for(const auto& obj: frag){
    //     if(obj > 0) dsets.make_set(obj);
    // }
    
    for(std::ptrdiff_t z=0; z<seg.shape(0); z++){
        for(std::ptrdiff_t y=0; y<seg.shape(1); y++){
            for(std::ptrdiff_t x=0; x<seg.shape(2); x++){
                const auto& obj0 = frag(z, y, x);
                if(z>0){
                    const auto& obj1 = frag(z-1, y, x);
                    if(obj0!=obj1 && seg(z,y,x)==seg(z-1,y,x) && obj0>0 && obj1>0){
                        // std::cout<<"merge :"<<obj0<<", "<<obj1<<std::endl;
                        // dsets.make_and_union_set(obj0, obj1);
                        pairs.emplace_back(obj0, obj1);
                    }
                }

                if(y>0){
                    const auto& obj1 = frag(z,y-1,x);
                    if(obj0!=obj1 && seg(z,y,x)==seg(z,y-1,x) && obj0>0 && obj1>0){
                        // std::cout<<"merge :"<<obj0<<", "<<obj1<<std::endl;
                        // dsets.make_and_union_set(obj0, obj1);
                        pairs.emplace_back(obj0, obj1);
                    }
                }

                if(x>0){
                    const auto& obj1 = frag(z, y, x-1);
                    if(obj0!=obj1 && seg(z,y,x)==seg(z,y,x-1) && obj0>0 && obj1>0){
                        // std::cout<<"merge :"<<obj0<<", "<<obj1<<std::endl;
                        // dsets.make_and_union_set(obj0, obj1);
                        pairs.emplace_back(obj0, obj1);
                    }
                }
            }
        }
    }

    std::cout<<"transform to array."<<std::endl;
    auto arr = seg_pairs_to_array(pairs);

    return arr;
}

} // namespace reneu