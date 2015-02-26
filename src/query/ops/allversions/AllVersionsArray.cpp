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
 * AllVersionsArray.cpp
 *
 *  Created on: Feb 5, 2011
 *      Author: Knizhnik
 */

#include "AllVersionsArray.h"
#include "array/DBArray.h"

namespace scidb {

using namespace boost;

inline Coordinates const& prependVersion(Coordinates& outPos, Coordinates const& inPos, VersionID version)
{
    outPos.clear();
    outPos.push_back(version);
    outPos.insert(outPos.end(), inPos.begin(), inPos.end());
    return outPos;
}

Coordinates const& AllVersionsChunkIterator::getPosition()
{
    return prependVersion(outPos, inputIterator->getPosition(), currVersion);
}

bool AllVersionsChunkIterator::setPosition(Coordinates const& pos)
{
    if (VersionID(pos[0]) != currVersion) {
        return false;
    }
    return inputIterator->setPosition(Coordinates(pos.begin()+1, pos.end()));
}

AllVersionsChunkIterator::AllVersionsChunkIterator(DelegateChunk const* chunk, int iterationMode, VersionID version)
: DelegateChunkIterator(chunk, iterationMode),
  currVersion(version)
{
}

void AllVersionsChunk::setInputChunk(ConstChunk const& inputChunk, VersionID version)
{
    DelegateChunk::setInputChunk(inputChunk);
    isClone = !inputChunk.isSparse();
    currVersion = version;
    prependVersion(firstPos, inputChunk.getFirstPosition(false), version);
    prependVersion(lastPos, inputChunk.getLastPosition(false), version);
    prependVersion(firstPosWithOverlap, inputChunk.getFirstPosition(true), version);
    prependVersion(lastPosWithOverlap, inputChunk.getLastPosition(true), version);
}

Coordinates const& AllVersionsChunk::getFirstPosition(bool withOverlap) const
{
    return withOverlap ? firstPosWithOverlap : firstPos;
}

Coordinates const& AllVersionsChunk::getLastPosition(bool withOverlap) const
{
    return withOverlap ? lastPosWithOverlap : lastPos;
}

AllVersionsChunk::AllVersionsChunk(DelegateArray const& array, DelegateArrayIterator const& iterator, AttributeID attrID)
  : DelegateChunk(array, iterator, attrID, true)
{
}

AllVersionsArrayIterator::AllVersionsArrayIterator(AllVersionsArray const& arr, AttributeID attrID, boost::shared_ptr<ConstArrayIterator> inputIterator)
: DelegateArrayIterator(arr, attrID, inputIterator),
  array(arr)
{
    reset();
}

boost::shared_ptr<Query>
AllVersionsArrayIterator::getQuery()
{
   return array._query.lock();
}

ConstChunk const& AllVersionsArrayIterator::getChunk()
{
    if (!hasCurrent)
        throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
    if (!chunkInitialized) { 
        ((AllVersionsChunk&)*chunk).setInputChunk(inputIterator->getChunk(), currVersion);
        chunkInitialized = true;
    }
    return *chunk;
}

bool AllVersionsArrayIterator::end()
{
    return !hasCurrent;
}

void AllVersionsArrayIterator::operator ++()
{
    if (!hasCurrent)
        throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
    boost::shared_ptr<Query> query(getQuery());
    chunkInitialized = false;
    ++(*inputIterator);
    while (inputIterator->end()) {
        if (currVersion == array.versions.size()) {
            hasCurrent = false;
            return;
        }
        inputIterator.reset();
        inputVersion = shared_ptr<Array>(new DBArray(array.getVersionName(++currVersion), query));
        inputIterator = inputVersion->getConstIterator(attr);
    }
}

Coordinates const& AllVersionsArrayIterator::getPosition()
{
    if (!hasCurrent)
        throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
    return prependVersion(outPos, inputIterator->getPosition(), currVersion);
}

bool AllVersionsArrayIterator::setPosition(Coordinates const& pos)
{
    boost::shared_ptr<Query> query(getQuery());
    currVersion = pos[0];
    chunkInitialized = false;
    if (currVersion < 1 || currVersion > array.versions.size()) {
        return hasCurrent = false;
    }
    inputIterator.reset();
    inputVersion = shared_ptr<Array>(new DBArray(array.getVersionName(currVersion), query));
    inputIterator = inputVersion->getConstIterator(attr);
    return hasCurrent = inputIterator->setPosition(Coordinates(pos.begin()+1, pos.end()));

}

void AllVersionsArrayIterator::reset()
{
    boost::shared_ptr<Query> query(getQuery());
    hasCurrent = false;
    chunkInitialized = false;
    for (currVersion = 1; currVersion <= array.versions.size(); currVersion++) {
        inputIterator.reset();
        inputVersion = shared_ptr<Array>(new DBArray(array.getVersionName(currVersion), query));
        inputIterator = inputVersion->getConstIterator(attr);
        if (!inputIterator->end()) {
            hasCurrent = true;
            return;
        }
    }
}

string AllVersionsArray::getVersionName(VersionID version) const
{
    std::stringstream ss;
    ss << desc.getName() << "@" << version;
    return ss.str();
}

DelegateArrayIterator* AllVersionsArray::createArrayIterator(AttributeID id) const
{
    return new AllVersionsArrayIterator(*this, id, shared_ptr<ConstArrayIterator>());
}

DelegateChunk* AllVersionsArray::createChunk(DelegateArrayIterator const* iterator, AttributeID id) const
{
    return new AllVersionsChunk(*this, *iterator, id);
}

DelegateChunkIterator* AllVersionsArray::createChunkIterator(DelegateChunk const* chunk, int iterationMode) const
{
    return new AllVersionsChunkIterator(chunk, iterationMode, ((AllVersionsChunk const*)chunk)->currVersion);
}

bool AllVersionsArray::supportsRandomAccess() const
{
    return true;
}

AllVersionsArray::AllVersionsArray(ArrayDesc const& arrayDesc, vector<VersionDesc> const& versionIds,  boost::shared_ptr<Query>& query)
: DelegateArray(arrayDesc, shared_ptr<Array>(), true),
  versions(versionIds),
  _query(query)
{
   assert(query);
}

}
