/*
**
* BEGIN_COPYRIGHT
*
* This file is part of SciDB.
* Copyright (C) 2008-2012 SciDB, Inc.
*
* SciDB is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation version 3 of the License.
*
* SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the GNU General Public License for the complete license terms.
*
* You should have received a copy of the GNU General Public License
* along with SciDB.  If not, see <http://www.gnu.org/licenses/>.
*
* END_COPYRIGHT
*/

/*
 * MergeSortArray.cpp
 *
 *  Created on: Sep 23, 2010
 */
#include "MergeSortArray.h"
#include "system/SystemCatalog.h"
#include "network/NetworkManager.h"

namespace scidb
{
    using namespace std;

inline size_t getArrayLength(DimensionDesc const& dim, size_t nodeId, size_t nNodes)
    {
        if (dim.getLength() == 0) {
            return 0;
        }
        uint64_t length = dim.getLength() / (dim.getChunkInterval()*nNodes) * dim.getChunkInterval();
        uint64_t rest = dim.getLength() % (dim.getChunkInterval()*nNodes);
        if (rest >= dim.getChunkInterval() * nodeId) {
            rest -= dim.getChunkInterval() * nodeId;
            if (rest > dim.getChunkInterval()) {
                rest = dim.getChunkInterval();
            }
            length += rest;
        }
        return (size_t)length;
    }

    ConstChunk const& MergeSortArrayIterator::getChunk()
    {
        return array.getChunk(attr, currChunkIndex);
    }

    bool MergeSortArrayIterator::end()
    {
        if (!hasCurrent) {
            hasCurrent = array.moveNext(currChunkIndex);
        }
        return !hasCurrent;
    }

    void MergeSortArrayIterator::operator ++()
    {
        if (end())
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        hasCurrent = false;
        currChunkIndex += 1;
    }

    Coordinates const& MergeSortArrayIterator::getPosition()
    {
        return getChunk().getFirstPosition(false);
    }

    MergeSortArrayIterator::MergeSortArrayIterator(MergeSortArray& arr, AttributeID id)
    : array(arr), attr(id), hasCurrent(false), currChunkIndex(1)
    {
    }

    ArrayDesc const& MergeSortArray::getArrayDesc() const
    {
        return desc;
    }

    boost::shared_ptr<ConstArrayIterator> MergeSortArray::getConstIterator(AttributeID attr) const
    {
        return attributes[attr].iterator;
    }

    MergeSortArray::MergeSortArray(const boost::shared_ptr<Query>& query,
                                   ArrayDesc const& array,
                                   std::vector< boost::shared_ptr<Array> > const& inputArrays,
                                   vector<Key> const& keys, bool local)
    : desc(array),
      currChunkIndex(0),
      comparator(keys, array),
      chunkPos(1),
      chunkSize(array.getDimensions()[0].getChunkInterval()),
      isLocal(local),
      input(inputArrays),
      streams(inputArrays.size()),
      attributes(array.getAttributes().size()),
      _query(query)
    {
        chunkPos[0] = array.getDimensions()[0].getStart();
        size_t nAttrs = attributes.size();
        for (size_t i = 0; i < nAttrs; i++) {
           attributes[i].iterator = boost::shared_ptr<MergeSortArrayIterator>(new MergeSortArrayIterator(*this, i));
        }
        size_t nNodes = query->getNodesCount();
        for (size_t i = 0, n = streams.size(); i < n; i++) {
            streams[i].inputArrayIterators.resize(nAttrs);
            streams[i].inputChunkIterators.resize(nAttrs);
            streams[i].tuple.resize(nAttrs);
            streams[i].endOfStream = true;
            streams[i].size = isLocal ? (size_t)-1 : getArrayLength(array.getDimensions()[0], i, nNodes);
            if (streams[i].size > 0) {
                for (size_t j = 0; j < nAttrs; j++) {
                    streams[i].inputArrayIterators[j] = inputArrays[i]->getConstIterator(j);
                    while (!streams[i].inputArrayIterators[j]->end()) {
                        streams[i].inputChunkIterators[j] = streams[i].inputArrayIterators[j]->getChunk().getConstIterator();
                        if (!streams[i].inputChunkIterators[j]->end()) {
                            streams[i].tuple[j] = streams[i].inputChunkIterators[j]->getItem();
                            streams[i].endOfStream = false;
                            break;
                        }
                        ++(*streams[i].inputArrayIterators[j]);
                    }
                }
                if (!streams[i].endOfStream) {
                    permutation.push_back(i);
                }
            }
        }
        iqsort(&permutation[0], permutation.size(), *this);
    }

    int MergeSortArray::binarySearch(Tuple const& tuple) {
        int l = 0, r = permutation.size();
        while (l < r) {
            int m = (l + r) >> 1;
            if (comparator.compare(streams[permutation[m]].tuple, tuple) > 0) {
                l = m + 1;
            } else {
                r = m;
            }
        }
        return r;
    }

    bool MergeSortArray::moveNext(size_t chunkIndex)
    {
        if (chunkIndex > currChunkIndex+1)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_OP_SORT_ERROR3);
        if (chunkIndex <= currChunkIndex) {
            return true;
        }
        size_t nAttrs = attributes.size();
        vector< boost::shared_ptr<ChunkIterator> > chunkIterators(nAttrs);
        boost::shared_ptr<Query> query(_query.lock());

        while (permutation.size() != 0) {
            if (!chunkIterators[0]) {
                for (size_t i = 0; i < nAttrs; i++) {
                    Address addr(desc.getId(), i, chunkPos);
                    MemChunk& chunk = attributes[i].chunks[chunkIndex % CHUNK_HISTORY_SIZE];
                    chunk.initialize(this, &desc, addr, desc.getAttributes()[i].getDefaultCompressionMethod());
                    chunkIterators[i] = chunk.getIterator(query, ChunkIterator::NO_EMPTY_CHECK);
                }
                chunkPos[0] += chunkSize;
                currChunkIndex += 1;
            }
            if (chunkIterators[0]->end()) {
                 for (size_t i = 0; i < nAttrs; i++) {
                    chunkIterators[i]->flush();
                }
                return true;
            }
            int min = permutation.back();
            permutation.pop_back();
            if (--streams[min].size == 0) {
                streams[min].endOfStream = true;
            }
            for (size_t i = 0; i < nAttrs; i++) {
                chunkIterators[i]->writeItem(streams[min].tuple[i]);
                ++(*chunkIterators[i]);
                if (!streams[min].endOfStream) {
                    ++(*streams[min].inputChunkIterators[i]);
                    while (streams[min].inputChunkIterators[i]->end()) {
                        streams[min].inputChunkIterators[i].reset();
                        ++(*streams[min].inputArrayIterators[i]);
                        if (!streams[min].inputArrayIterators[i]->end()) {
                            streams[min].inputChunkIterators[i] = streams[min].inputArrayIterators[i]->getChunk().getConstIterator();
                        } else {
                            streams[min].endOfStream = true;
                            break;
                        }
                    }
                    if (!streams[min].endOfStream) {
                        streams[min].tuple[i] = streams[min].inputChunkIterators[i]->getItem();
                    }
                }
            }
            if (!streams[min].endOfStream) {
                permutation.insert(permutation.begin() + binarySearch(streams[min].tuple), min);
            }
        }
        if (!chunkIterators[0]) {
            return false;
        }
        for (size_t i = 0; i < nAttrs; i++) {
            chunkIterators[i]->flush();
        }
        return true;
    }

    bool MergeSortArray::supportsRandomAccess() const
    {
        return false;
    }

    ConstChunk const& MergeSortArray::getChunk(AttributeID attr, size_t chunkIndex)
    {
        if (chunkIndex > currChunkIndex || chunkIndex + CHUNK_HISTORY_SIZE <= currChunkIndex)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_OP_SORT_ERROR4);
        return attributes[attr].chunks[chunkIndex % CHUNK_HISTORY_SIZE];
    }
}
