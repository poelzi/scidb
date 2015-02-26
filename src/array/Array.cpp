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

/**
 * @file Array.cpp
 *
 * @brief Array API
 *
 * @author Konstantin Knizhnik <knizhnik@garret.ru>
 */

#include <vector>
#include <string.h>
#include <boost/assign.hpp>

#include "array/MemArray.h"
#include "array/EmbeddedArray.h"
#include "array/RLE.h"
#include "system/Exceptions.h"
#include "query/FunctionDescription.h"
#include "query/TypeSystem.h"
#include "query/Statistics.h"
#ifndef SCIDB_CLIENT
#include "system/Config.h"
#endif
#include "system/SciDBConfigOptions.h"

#include <log4cxx/logger.h>
#include <query/ops/redimension/SyntheticDimHelper.h>
#include <query/Operator.h>

using namespace boost::assign;

namespace scidb
{

    // Logger for operator. static to prevent visibility of variable outside of file
    static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.array.Array"));

    void* SharedBuffer::getData() const
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "SharedBuffer::getData";
    }

    size_t SharedBuffer::getSize() const
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "SharedBuffer::getSize";
    }

    void SharedBuffer::allocate(size_t size)
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "SharedBuffer::allocate";
    }

    void SharedBuffer::reallocate(size_t size)
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "SharedBuffer::reallocate";
    }

    void SharedBuffer::free()
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "SharedBuffer::free";
    }

    bool SharedBuffer::pin() const
    {
        return false;
    }

    void SharedBuffer::unPin() const
    {
    }


    void* CompressedBuffer::getData() const
    {
        return data;
    }

    size_t CompressedBuffer::getSize() const
    {
        return compressedSize;
    }

    void CompressedBuffer::allocate(size_t size)
    {
        data = ::malloc(size);
        if (data == NULL) { 
            throw SYSTEM_EXCEPTION(SCIDB_SE_NO_MEMORY, SCIDB_LE_CANT_ALLOCATE_MEMORY);
        }
        compressedSize = size;
        currentStatistics->allocatedSize += size;
        currentStatistics->allocatedChunks++;
    }

    void CompressedBuffer::reallocate(size_t size)
    {
        data = ::realloc(data, size);
        if (data == NULL) { 
            throw SYSTEM_EXCEPTION(SCIDB_SE_NO_MEMORY, SCIDB_LE_CANT_ALLOCATE_MEMORY);
        }
        compressedSize = size;
        currentStatistics->allocatedSize += size;
        currentStatistics->allocatedChunks++;
    }

    void CompressedBuffer::free()
    {
       ::free(data);
       data = NULL;
    }

    bool CompressedBuffer::pin() const
    {
        ((CompressedBuffer*)this)->accessCount += 1;
        currentStatistics->pinnedSize += compressedSize;
        currentStatistics->pinnedChunks++;
        return true;
    }

    void CompressedBuffer::unPin() const
    {
        CompressedBuffer& self = *(CompressedBuffer*)this;
        assert(self.accessCount > 0);
        if (--self.accessCount == 0) {
            self.free();
        }
    }

    int CompressedBuffer::getCompressionMethod() const
    {
        return compressionMethod;
    }

    void CompressedBuffer::setCompressionMethod(int m)
    {
        compressionMethod = m;
    }

    size_t CompressedBuffer::getDecompressedSize() const
    {
        return decompressedSize;
    }

    void CompressedBuffer::setDecompressedSize(size_t size)
    {
        decompressedSize = size;
    }

    CompressedBuffer::CompressedBuffer(void* compressedData, int compressionMethod, size_t compressedSize, size_t decompressedSize)
    {
        data = compressedData;
        this->compressionMethod = compressionMethod;
        this->compressedSize = compressedSize;
        this->decompressedSize = decompressedSize;
        accessCount = 0;
    }

    CompressedBuffer::CompressedBuffer()
    {
        data = NULL;
        compressionMethod = 0;
        compressedSize = 0;
        decompressedSize = 0;
        accessCount = 0;
    }

    CompressedBuffer::~CompressedBuffer()
    {
        free();
    }

    size_t ConstChunk::getBitmapSize() const
    {
        if (isRLE() && isMaterialized() && !getAttributeDesc().isEmptyIndicator()) { 
            PinBuffer scope(*this);
            ConstRLEPayload payload((char*)getData());
            return getSize() - payload.packedSize();
        }
        return 0;
    }
    
    Coordinates ConstChunk::getHighBoundary(bool withOverlap) const
    {
        boost::shared_ptr<ConstChunkIterator> i = getConstIterator(ConstChunkIterator::IGNORE_EMPTY_CELLS | (withOverlap ? 0 : ConstChunkIterator::IGNORE_OVERLAPS));
        Coordinates high = getFirstPosition(withOverlap);
        size_t nDims = high.size();
        while (!i->end()) { 
            Coordinates const& pos = i->getPosition();
            for (size_t j = 0; j < nDims; j++) {
                if (pos[j] > high[j]) { 
                    high[j] = pos[j];
                }
            }
            ++(*i);
        }
        return high;
    }

    Coordinates ConstChunk::getLowBoundary(bool withOverlap) const
    {
        boost::shared_ptr<ConstChunkIterator> i = getConstIterator(ConstChunkIterator::IGNORE_EMPTY_CELLS | (withOverlap ? 0 : ConstChunkIterator::IGNORE_OVERLAPS));
        Coordinates low = getLastPosition(withOverlap);
        size_t nDims = low.size();
        while (!i->end()) { 
            Coordinates const& pos = i->getPosition();
            for (size_t j = 0; j < nDims; j++) {
                if (pos[j] < low[j]) { 
                    low[j] = pos[j];
                }
            }
            ++(*i);
        }
        return low;
    }

    ConstChunk const* ConstChunk::getBitmapChunk() const
    {
        return this;
    }

    void ConstChunk::makeClosure(Chunk& closure, boost::shared_ptr<ConstRLEEmptyBitmap> const& emptyBitmap) const
    {
        PinBuffer scope(*this);
        closure.allocate(getSize() + emptyBitmap->packedSize());
        memcpy(closure.getData(), getData(), getSize());
        emptyBitmap->pack((char*)closure.getData() + getSize());
    }

    ConstChunk* ConstChunk::materialize() const
    {
        if (materializedChunk == NULL || materializedChunk->getFirstPosition(false) != getFirstPosition(false)) { 
            if (materializedChunk == NULL) { 
                ((ConstChunk*)this)->materializedChunk = new MemChunk();
            }
            materializedChunk->initialize(*this);
            materializedChunk->setBitmapChunk((Chunk*)getBitmapChunk());
            boost::shared_ptr<ConstChunkIterator> src 
                = getConstIterator(ChunkIterator::IGNORE_DEFAULT_VALUES|ChunkIterator::IGNORE_EMPTY_CELLS|ChunkIterator::INTENDED_TILE_MODE
                                   |(materializedChunk->getArrayDesc().hasOverlap() ? 0 : ChunkIterator::IGNORE_OVERLAPS));
            shared_ptr<Query> emptyQuery;
            boost::shared_ptr<ChunkIterator> dst 
                = materializedChunk->getIterator(emptyQuery, 
                                                 (src->getMode() & ChunkIterator::TILE_MODE)|ChunkIterator::ChunkIterator::NO_EMPTY_CHECK|ChunkIterator::SEQUENTIAL_WRITE);
            bool vectorMode = src->supportsVectorMode() && dst->supportsVectorMode();
            src->setVectorMode(vectorMode);
            dst->setVectorMode(vectorMode);
            size_t count = 0;
            while (!src->end()) {
                if (!dst->setPosition(src->getPosition())) { 
                    Coordinates const& pos = src->getPosition();
                    dst->setPosition(pos);
                    throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_OPERATION_FAILED) << "setPosition";
                }
                dst->writeItem(src->getItem());
                count += 1;
                ++(*src);
            }
            if (!vectorMode && !getArrayDesc().hasOverlap()) {
                materializedChunk->setCount(count);
            }
            dst->flush();
        }
        return materializedChunk;
    }

    void ConstChunk::compress(CompressedBuffer& buf, boost::shared_ptr<ConstRLEEmptyBitmap>& emptyBitmap) const
    {
        materialize()->compress(buf, emptyBitmap);
    }

    void* ConstChunk::getData() const
    {
        return materialize()->getData();
    }

    size_t ConstChunk::getSize() const
    {
        return materialize()->getSize();
    }

    bool ConstChunk::pin() const
    {
        return false;
    }

    void ConstChunk::unPin() const
    {
        assert(typeid(*this) != typeid(ConstChunk));
    }

    void Chunk::decompress(CompressedBuffer const& buf)
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Chunk::decompress";
    }

    void Chunk::merge(ConstChunk const& with, boost::shared_ptr<Query>& query)
    {
        if (getDiskChunk() != NULL)
            throw USER_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_CHUNK_ALREADY_EXISTS);
        setCount(0); // unknown
        AttributeDesc const& attr = getAttributeDesc();
        char* dst = (char*)getData();
        Value const& defaultValue = attr.getDefaultValue();

        // If it is in the middle of a redimension, and if there is a synthetic dimension, use the following algorithm:
        // (a) Build an auxiliary structure describing #elements in each cell, not including the synthetic dim.
        // (b) For each source element, adjust its coordinates by increasing the coordinate in the synthetic dim.
        //
        shared_ptr<RedimInfo> redimInfo;
        boost::shared_ptr<SGContext> sgCtx = dynamic_pointer_cast<SGContext>(query->getOperatorContext());
        if (sgCtx) {
            redimInfo = sgCtx->_redimInfo;
        }
        if (redimInfo && redimInfo->_hasSynthetic) {
            // During redim, there is always a empty tag, and the chunk can't be sparse.
            assert(getArrayDesc().getEmptyBitmapAttribute());
            assert(!isSparse());

            // Build the auxiliary structure.
            SyntheticDimHelper syntheticDimHelper(redimInfo->_dimSynthetic, redimInfo->_dim.getStart());
            shared_ptr<MapCoordToCount> mapCoordToCount = make_shared<MapCoordToCount>();
            syntheticDimHelper.updateMapCoordToCount(mapCoordToCount, reinterpret_cast<LruMemChunk*>(this));

            // Iterate through the src, update each coord, and write to dest.
            // Note that default values can't be ignored. Otherwise the coordinate in the synthetic dimension would mess up.
            shared_ptr<ConstChunkIterator> srcIterator = with.getConstIterator(ChunkIterator::IGNORE_EMPTY_CELLS);
            shared_ptr<ChunkIterator> dstIterator = getIterator(query, ChunkIterator::APPEND_CHUNK|ChunkIterator::NO_EMPTY_CHECK);
            while (!srcIterator->end()) {
                Coordinates coord = srcIterator->getPosition();
                syntheticDimHelper.calcNewCoord(coord, mapCoordToCount);
                if (!dstIterator->setPosition(coord)) {
                    char buf[100];
                    sprintf(buf, "setPosition failed; the object has synthetic-dim coord=%ld; chunk interval is %d.",
                            coord[redimInfo->_dimSynthetic], redimInfo->_dim.getChunkInterval());
                    throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_OPERATION_FAILED) << buf;
                }
                Value const& value = srcIterator->getItem();
                dstIterator->writeItem(value);
                ++(*srcIterator);
            }
            dstIterator->flush();

            return;
        }

        // If dst already has data, and
        // Either
        //   (a) there is a redimInfo (regardless to whether there is a synthetic dim);
        // Or
        //   (b) can't merge by bitwise-or.
        // Then we have to iterate through the items.
        //
        // If there is a redimInfo (although no synthetic dim), we have to iterate through the items for the following reason:
        // A conflict (i.e. two items falling into the same cell) should be resolved by choosing one of them, not bitwise-or them.
        if ( (dst != NULL) &&
                (
                        ! isPossibleToMergeByBitwiseOr()
                        ||
                        ! with.isPossibleToMergeByBitwiseOr()
                        ||
                        redimInfo
                )
        )
        {
            int sparseMode = isSparse() ? ChunkIterator::SPARSE_CHUNK : 0;
            boost::shared_ptr<ChunkIterator> dstIterator = getIterator(query, sparseMode|ChunkIterator::APPEND_CHUNK|ChunkIterator::NO_EMPTY_CHECK);
            boost::shared_ptr<ConstChunkIterator> srcIterator = with.getConstIterator(ChunkIterator::IGNORE_EMPTY_CELLS|ChunkIterator::IGNORE_DEFAULT_VALUES);
            if (getArrayDesc().getEmptyBitmapAttribute() != NULL) { 
                while (!srcIterator->end()) {
                    if (!dstIterator->setPosition(srcIterator->getPosition()))
                        throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_OPERATION_FAILED) << "setPosition";
                    Value const& value = srcIterator->getItem();
                    dstIterator->writeItem(value);
                    ++(*srcIterator);
                }
            } else { // ignore default values
                while (!srcIterator->end()) {
                    Value const& value = srcIterator->getItem();
                    if (value != defaultValue) {
                        if (!dstIterator->setPosition(srcIterator->getPosition()))
                            throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_OPERATION_FAILED) << "setPosition";
                        dstIterator->writeItem(value);
                    }
                    ++(*srcIterator);
                }            
            }
            dstIterator->flush();
        }

        // Otherwise, we can merge faster.
        else {
            PinBuffer scope(with);
            char* src = (char*)with.getData();
            if (dst == NULL) {
                allocateAndCopy(src, with.getSize(), with.isSparse(), with.isRLE(), with.count(), query);
            } else {
                mergeByBitwiseOr(src, with.getSize(), query);
            }
        }
    }

    void Chunk::aggregateMerge(ConstChunk const& with, AggregatePtr const& aggregate, boost::shared_ptr<Query>& query)
    {
        if (getDiskChunk() != NULL)
            throw USER_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_CHUNK_ALREADY_EXISTS);

        if (isReadOnly())
            throw USER_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_CANT_UPDATE_READ_ONLY_CHUNK);

        AttributeDesc const& attr = getAttributeDesc();

        if (aggregate->getStateType().typeId() != attr.getType())
            throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_TYPE_MISMATCH_BETWEEN_AGGREGATE_AND_CHUNK);

        if (!attr.isNullable())
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_AGGREGATE_STATE_MUST_BE_NULLABLE);//enforce equivalency w above merge()

        setCount(0);
        char* dst = (char*)getData();
        if (dst != NULL)
        {
            int sparseMode = isSparse() ? ChunkIterator::SPARSE_CHUNK : 0;
            boost::shared_ptr<ChunkIterator>dstIterator = getIterator(query, sparseMode|ChunkIterator::APPEND_CHUNK|ChunkIterator::NO_EMPTY_CHECK);
            boost::shared_ptr<ConstChunkIterator> srcIterator = with.getConstIterator(ChunkIterator::IGNORE_NULL_VALUES);
            while (!srcIterator->end())
            {
                Value& val = srcIterator->getItem();
                if (!val.isNull())
                {
                    if (!dstIterator->setPosition(srcIterator->getPosition()))
                        throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_OPERATION_FAILED) << "setPosition";
                    Value& val2 = dstIterator->getItem();
                    if (!val2.isNull())
                    {
                        aggregate->merge(val, val2);

                    }
                    dstIterator->writeItem(val);
                }
                ++(*srcIterator);
            }
            dstIterator->flush();
        }
        else
        {
            PinBuffer scope(with);
            allocateAndCopy((char*)with.getData(), with.getSize(), with.isSparse(), with.isRLE(), with.count(), query);
        }
    }

    void Chunk::nonEmptyableAggregateMerge(ConstChunk const& with, AggregatePtr const& aggregate, boost::shared_ptr<Query>& query)
    {
        if (getDiskChunk() != NULL)
        {
            throw USER_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_CHUNK_ALREADY_EXISTS);
        }

        if (isReadOnly())
        {
            throw USER_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_CANT_UPDATE_READ_ONLY_CHUNK);
        }

        AttributeDesc const& attr = getAttributeDesc();

        if (aggregate->getStateType().typeId() != attr.getType())
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_TYPE_MISMATCH_BETWEEN_AGGREGATE_AND_CHUNK);
        }

        if (!attr.isNullable())
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_AGGREGATE_STATE_MUST_BE_NULLABLE);//enforce equivalency w above merge()
        }

        assert(isRLE() && with.isRLE());

        char* dst = static_cast<char*>(getData());
        PinBuffer scope(with);
        if (dst != NULL)
        {
            boost::shared_ptr<ChunkIterator>dstIterator = getIterator(query, ChunkIterator::APPEND_CHUNK|ChunkIterator::NO_EMPTY_CHECK);
            CoordinatesMapper mapper(with);
            ConstRLEPayload inputPayload( static_cast<char*>(with.getData()));
            ConstRLEPayload::iterator inputIter(&inputPayload);
            Value val;
            position_t lpos;
            Coordinates cpos(mapper.getNumDims());

            while (!inputIter.end())
            {
                //Missing Reason 0 is reserved by the system meaning "group does not exist".
                //All other Missing Reasons may be used by the aggregate if needed.
                if (inputIter.isNull() && inputIter.getMissingReason() == 0)
                {
                    inputIter.toNextSegment();
                }
                else
                {
                    inputIter.getItem(val);
                    lpos = inputIter.getPPos();
                    mapper.pos2coord(lpos, cpos);
                    if (!dstIterator->setPosition(cpos))
                        throw SYSTEM_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_OPERATION_FAILED) << "setPosition";
                    Value& val2 = dstIterator->getItem();
                    if (val2.getMissingReason() != 0)
                    {
                        aggregate->merge(val, val2);
                    }
                    dstIterator->writeItem(val);
                    ++inputIter;
                }
            }
            dstIterator->flush();
        }
        else {
            allocateAndCopy(static_cast<char*>(with.getData()), with.getSize(), with.isSparse(), with.isRLE(), with.count(), query);
        }
    }

    void Chunk::setSparse(bool)
    {
    }

    void Chunk::setRLE(bool)
    {
    }

    void Chunk::write(boost::shared_ptr<Query>& query)
    {
    }

    void Chunk::setCount(size_t)
    {
    }

    void Chunk::truncate(Coordinate lastCoord)
    {
    }

    bool ConstChunk::contains(Coordinates const& pos, bool withOverlap) const
    {
        Coordinates const& first = getFirstPosition(withOverlap);
        Coordinates const& last = getLastPosition(withOverlap);
        for (size_t i = 0, n = first.size(); i < n; i++) {
            if (pos[i] < first[i] || pos[i] > last[i]) {
                return false;
            }
        }
        return true;
    }

    bool ConstChunk::isCountKnown() const
    {
        return getArrayDesc().getEmptyBitmapAttribute() == NULL
            || (materializedChunk && materializedChunk->isCountKnown());
    }

    size_t ConstChunk::count() const
    {
        if (getArrayDesc().getEmptyBitmapAttribute() == NULL) {
            return getNumberOfElements(false);
        }
        if (materializedChunk) {
            return materializedChunk->count();
        }
        shared_ptr<ConstChunkIterator> i = getConstIterator();
        size_t n = 0;
        while (!i->end()) {
            ++(*i);
            n += 1;
        }
        return n;
    }

    size_t ConstChunk::getNumberOfElements(bool withOverlap) const
    {
        Coordinates low = getFirstPosition(withOverlap);
        Coordinates high = getLastPosition(withOverlap);
        return getChunkNumberOfElements(low, high);
    }

    ConstIterator::~ConstIterator() {}

    bool ConstChunkIterator::supportsVectorMode() const
    {
        return false;
    }

    void ConstChunkIterator::setVectorMode(bool enabled)
    {
        if (enabled)
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "setVectorMode";
    }

    Coordinates const& ConstChunkIterator::getFirstPosition()
    {
        return getChunk().getFirstPosition((getMode() & IGNORE_OVERLAPS) == 0);
    }

    Coordinates const& ConstChunkIterator::getLastPosition()
    {
        return getChunk().getLastPosition((getMode() & IGNORE_OVERLAPS) == 0);
    }

        bool ConstChunkIterator::forward(uint64_t direction)
    {
        Coordinates pos = getPosition();
        Coordinates const& last = getLastPosition();
        do {
            for (size_t i = 0; direction != 0; i++, direction >>= 1) {
                if (direction & 1) {
                    if (++pos[i] > last[i]) {
                        return false;
                    }
                }
            }
        } while (!setPosition(pos));
        return true;
    }

        bool ConstChunkIterator::backward(uint64_t direction)
    {
        Coordinates pos = getPosition();
        Coordinates const& first = getFirstPosition();
        do {
            for (size_t i = 0; direction != 0; i++, direction >>= 1) {
                if (direction & 1) {
                    if (--pos[i] < first[i]) {
                        return false;
                    }
                }
            }
        } while (!setPosition(pos));
        return true;
    }

    bool ConstChunk::isPlain() const
    {
        Dimensions const& dims = getArrayDesc().getDimensions();
        for (size_t i = 0, n = dims.size(); i < n; i++) {
            if (dims[i].getChunkOverlap() != 0) {
                return false;
            }
        }
        return !isSparse()
            && !isRLE()
            && !getAttributeDesc().isNullable()
            && !TypeLibrary::getType(getAttributeDesc().getType()).variableSize()
            && (getAttributeDesc().isEmptyIndicator() || getArrayDesc().getEmptyBitmapAttribute() == NULL);
    }

    bool ConstChunk::isSolid() const
    {
        Dimensions const& dims = getArrayDesc().getDimensions();
        Coordinates const& first = getFirstPosition(false);
        Coordinates const& last = getLastPosition(false);
        for (size_t i = 0, n = dims.size(); i < n; i++) {
            if (dims[i].getChunkOverlap() != 0 || size_t(last[i] - first[i] + 1) != dims[i].getChunkInterval()) {
                return false;
            }
        }
        return !isSparse()
            && !getAttributeDesc().isNullable()
            && !TypeLibrary::getType(getAttributeDesc().getType()).variableSize()
            && getArrayDesc().getEmptyBitmapAttribute() == NULL;
    }

    bool ConstChunk::isReadOnly() const
    {
        return true;
    }

    bool ConstChunk::isMaterialized() const
    {
        return false;
    }

    DBChunk const* ConstChunk::getDiskChunk() const
    {
        return NULL;
    }

    bool ConstChunk::isSparse() const
    {
        return false;
    }


    bool ConstChunk::isRLE() const
    {
#ifndef SCIDB_CLIENT
        return Config::getInstance()->getOption<bool>(CONFIG_RLE_CHUNK_FORMAT);
#else
        return false;
#endif
    }

    boost::shared_ptr<ConstRLEEmptyBitmap> ConstChunk::getEmptyBitmap() const
    {
        if (isRLE() && getAttributeDesc().isEmptyIndicator()/* && isMaterialized()*/) { 
            PinBuffer scope(*this);
            return boost::shared_ptr<ConstRLEEmptyBitmap>(scope.isPinned() ? new ConstRLEEmptyBitmap(*this) : new RLEEmptyBitmap(ConstRLEEmptyBitmap(*this)));
        }
        AttributeDesc const* emptyAttr = getArrayDesc().getEmptyBitmapAttribute();
        if (emptyAttr != NULL) {
            if (!emptyIterator) { 
                ((ConstChunk*)this)->emptyIterator = getArray().getConstIterator(emptyAttr->getId());
            }
            if (!emptyIterator->setPosition(getFirstPosition(false))) {
                throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_OPERATION_FAILED) << "setPosition";
            }
            ConstChunk const& bitmapChunk = emptyIterator->getChunk();
            PinBuffer scope(bitmapChunk);
            return boost::shared_ptr<ConstRLEEmptyBitmap>(new RLEEmptyBitmap(ConstRLEEmptyBitmap((char*)bitmapChunk.getData())));
        }
        return boost::shared_ptr<ConstRLEEmptyBitmap>();
    }


    ConstChunk::ConstChunk() : materializedChunk(NULL)
    {
    }

    ConstChunk::~ConstChunk()
    {
        delete materializedChunk; 
    }

    void Array::getOriginalPosition(std::vector<Value>& origCoords, Coordinates const& intCoords, const boost::shared_ptr<Query>& query) const
    {
        size_t nDims = intCoords.size();
        origCoords.resize(nDims);
        ArrayDesc const& desc = getArrayDesc();
        for (size_t i = 0; i < nDims; i++) { 
            origCoords[i] = desc.getOriginalCoordinate(i, intCoords[i], query);
        }
    }

    std::string const& Array::getName() const
    {
        return getArrayDesc().getName();
    }

    ArrayID Array::getHandle() const
    {
        return getArrayDesc().getId();
    }

    void Array::append(boost::shared_ptr<Array>& input, bool const vertical, set<Coordinates, CoordinatesLess>* newChunkCoordinates)
    {
        if (vertical) {
            for (size_t i = 0, n = getArrayDesc().getAttributes().size(); i < n; i++) {
                boost::shared_ptr<ArrayIterator> dst = getIterator(i);
                boost::shared_ptr<ConstArrayIterator> src = input->getConstIterator(i);
                while (!src->end())
                {
                    if(newChunkCoordinates && i == 0)
                    {
                        newChunkCoordinates->insert(src->getPosition());
                    }
                    dst->copyChunk(src->getChunk());
                    ++(*dst);
                    ++(*src);
                }
            }
        } else {
            size_t nAttrs = getArrayDesc().getAttributes().size();
            std::vector< boost::shared_ptr<ArrayIterator> > dstIterators(nAttrs);
            std::vector< boost::shared_ptr<ConstArrayIterator> > srcIterators(nAttrs);
            for (size_t i = 0; i < nAttrs; i++) {
                dstIterators[i] = getIterator(i);
                srcIterators[i] = input->getConstIterator(i);
            }
            while (!srcIterators[0]->end())
            {
                if(newChunkCoordinates)
                {
                    newChunkCoordinates->insert(srcIterators[0]->getPosition());
                }
                for (size_t i = 0; i < nAttrs; i++)
                {
                    boost::shared_ptr<ArrayIterator> dst = dstIterators[i];
                    boost::shared_ptr<ConstArrayIterator> src = srcIterators[i];
                    dst->copyChunk(src->getChunk());
                    ++(*dst);
                    ++(*src);
                }
            }
        }
    }

    boost::shared_ptr<CoordinateSet> Array::getChunkPositions() const
    {
        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_UNKNOWN_ERROR) << "calling getChunkPositions on an invalid array";
    }

    static char* copyStride(char* dst, char* src, Coordinates const& first, Coordinates const& last, Dimensions const& dims, size_t step, size_t attrSize, size_t c)
    {
        size_t n = dims[c].getChunkInterval();
        if (c+1 == dims.size()) {
            memcpy(dst, src, n*attrSize);
            src += n*attrSize;
        } else {
            step /= last[c] - first[c] + 1;
            for (size_t i = 0; i < n; i++) {
                src = copyStride(dst, src, first, last, dims, step, attrSize, c+1);
                dst += step*attrSize;
            }
        }
        return src;
    }

    size_t Array::extractData(AttributeID attrID, void* buf, Coordinates const& first, Coordinates const& last) const
    {
        ArrayDesc const& arrDesc =  getArrayDesc();
        AttributeDesc const& attrDesc = arrDesc.getAttributes()[attrID];
         Type attrType( TypeLibrary::getType(attrDesc.getType()));
        Dimensions const& dims = arrDesc.getDimensions();
        size_t nDims = dims.size();
        bool isNullable = attrDesc.isNullable();
        bool isEmptyable = arrDesc.getEmptyBitmapAttribute() != NULL;
        bool hasOverlap = false;
        bool aligned = true;
        if (attrType.variableSize())
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_EXTRACT_EXPECTED_FIXED_SIZE_ATTRIBUTE);

        if (attrType.bitSize() < 8)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_EXTRACT_UNEXPECTED_BOOLEAN_ATTRIBUTE);

        if (first.size() != nDims || last.size() != nDims)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_WRONG_NUMBER_OF_DIMENSIONS);

        size_t bufSize = 1;
        for (size_t j = 0; j < nDims; j++)
        {
            if (last[j] < first[j] || (first[j] - dims[j].getStart()) % dims[j].getChunkInterval() != 0) {
                throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_UNALIGNED_COORDINATES);
            }
            aligned &= (last[j] - dims[j].getStart() + 1) % dims[j].getChunkInterval() == 0;

            hasOverlap |= dims[j].getChunkOverlap() != 0;
            bufSize *= last[j] - first[j] + 1;
        }
        size_t attrSize = attrType.byteSize();
        memset(buf, 0, bufSize*attrSize);
        size_t nExtracted = 0;
        for (boost::shared_ptr<ConstArrayIterator> i = getConstIterator(attrID); !i->end(); ++(*i)) {
            size_t j, chunkOffs = 0;
            ConstChunk const& chunk = i->getChunk();
            Coordinates const& chunkPos = i->getPosition();
            for (j = 0; j < nDims; j++) {
                if (chunkPos[j] < first[j] || chunkPos[j] > last[j]) {
                    break;
                }
                chunkOffs *= last[j] - first[j] + 1;
                chunkOffs += chunkPos[j] - first[j];
            }
            if (j == nDims) {
                if (!aligned || hasOverlap || isEmptyable || isNullable || chunk.isRLE() || chunk.isSparse()) {
                    for (boost::shared_ptr<ConstChunkIterator> ci = chunk.getConstIterator(ChunkIterator::IGNORE_OVERLAPS|ChunkIterator::IGNORE_EMPTY_CELLS|ChunkIterator::IGNORE_NULL_VALUES);
                         !ci->end(); ++(*ci))
                    {
                        Value& v = ci->getItem();
                        if (!v.isNull()) { 
                            Coordinates const& itemPos = ci->getPosition();
                            size_t itemOffs = 0;
                            for (j = 0; j < nDims; j++) {
                                itemOffs *= last[j] - first[j] + 1;
                                itemOffs += itemPos[j] - first[j];
                            }
                            memcpy((char*)buf + itemOffs*attrSize, ci->getItem().data(), attrSize);
                        }
                    }
                } else {
                    PinBuffer scope(chunk);
                    copyStride((char*)buf + chunkOffs*attrSize, (char*)chunk.getData(), first, last, dims, bufSize, attrSize, 0);
                }
                nExtracted += 1;
            }
        }
        return nExtracted;
    }

    boost::shared_ptr<ArrayIterator> Array::getIterator(AttributeID attr)
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Array::getIterator";
    }


    boost::shared_ptr<ConstItemIterator> Array::getItemIterator(AttributeID attrID, int iterationMode) const
    {
        return boost::shared_ptr<ConstItemIterator>(new ConstItemIterator(*this, attrID, iterationMode));
    }

    bool ConstArrayIterator::setPosition(Coordinates const& pos)
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "ConstArrayIterator::setPosition";
    }

    void ConstArrayIterator::reset()
    {
        throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "ConstArrayIterator::reset";
    }

    void ArrayIterator::deleteChunk(Chunk& chunk)
    {
    }
    Chunk& ArrayIterator::updateChunk()
    {
        ConstChunk const& constChunk = getChunk();
        if (constChunk.isReadOnly())
            throw USER_EXCEPTION(SCIDB_SE_MERGE, SCIDB_LE_CANT_UPDATE_READ_ONLY_CHUNK);
        Chunk& chunk = (Chunk&)dynamic_cast<const Chunk&>(constChunk);
        chunk.pin();
        return chunk;
    }

    Chunk& ArrayIterator::copyChunk(ConstChunk const& chunk, boost::shared_ptr<ConstRLEEmptyBitmap>& emptyBitmap)
    {
        const Coordinates& pos = chunk.getFirstPosition(false);
        Chunk& outChunk = newChunk(pos, chunk.getCompressionMethod());

        //verify that the declared chunk intervals match. Otherwise the copy - could still work - but would either be an implicit reshape or outright dangerous
        SCIDB_ASSERT(chunk.getArrayDesc().getDimensions().size() == outChunk.getArrayDesc().getDimensions().size());
        for(size_t i = 0, n = chunk.getArrayDesc().getDimensions().size(); i < n; i++)
        {
            SCIDB_ASSERT(chunk.getArrayDesc().getDimensions()[i].getChunkInterval() == outChunk.getArrayDesc().getDimensions()[i].getChunkInterval());
        }

        try {
            boost::shared_ptr<Query> query(getQuery());
            outChunk.setSparse(chunk.isSparse());

            // If copying from an emptyable array to an non-emptyable array, we need to fill in the default values.
            size_t nAttrsChunk = chunk.getArrayDesc().getAttributes().size();
            size_t nAttrsOutChunk = outChunk.getArrayDesc().getAttributes().size();
            assert(nAttrsChunk >= nAttrsOutChunk);
            assert(nAttrsOutChunk+1 >= nAttrsChunk);
            bool emptyableToNonEmptyable = (nAttrsOutChunk+1 == nAttrsChunk);

            if (chunk.isMaterialized()
                    && chunk.getArrayDesc().hasOverlap() == outChunk.getArrayDesc().hasOverlap()
                    && chunk.getAttributeDesc().isNullable() == outChunk.getAttributeDesc().isNullable()
                    // TEMPORARY flag is set for NID array and we  have to preserve non-RLE format for it
                    && (chunk.isRLE() == outChunk.isRLE() || chunk.isSolid() || (chunk.getArrayDesc().getFlags() & ArrayDesc::TEMPORARY))
                    // if emptyableToNonEmptyable, we cannot use memcpy because we need to insert defaultvalues
                    && !emptyableToNonEmptyable
                    && chunk.getNumberOfElements(true) == outChunk.getNumberOfElements(true)
                )
            {
                PinBuffer scope(chunk);
                outChunk.setRLE(chunk.isRLE());
                if (emptyBitmap && chunk.getBitmapSize() == 0) { 
                    size_t size = chunk.getSize() + emptyBitmap->packedSize();
                    outChunk.allocate(size);
                    memcpy(outChunk.getData(), chunk.getData(), chunk.getSize());
                    emptyBitmap->pack((char*)outChunk.getData() + chunk.getSize());
                } else {
                    size_t size = emptyBitmap ? chunk.getSize() : chunk.getSize() - chunk.getBitmapSize();
                    outChunk.allocate(size);
                    memcpy(outChunk.getData(), chunk.getData(), size);
                }
                outChunk.setCount(chunk.isCountKnown() ? chunk.count() : 0);
                outChunk.write(query);
            } else {
                if (emptyBitmap) { 
                    chunk.makeClosure(outChunk, emptyBitmap);
                    outChunk.write(query);
                } else { 
                    boost::shared_ptr<ConstChunkIterator> src = chunk.getConstIterator(
                            ChunkIterator::IGNORE_EMPTY_CELLS|ChunkIterator::INTENDED_TILE_MODE|(outChunk.getArrayDesc().hasOverlap() ? 0 : ChunkIterator::IGNORE_OVERLAPS));
                    boost::shared_ptr<ChunkIterator> dst = outChunk.getIterator(query,
                            (src->getMode() & ChunkIterator::TILE_MODE)|ChunkIterator::NO_EMPTY_CHECK|(chunk.isSparse()?ChunkIterator::SPARSE_CHUNK:0)|ChunkIterator::SEQUENTIAL_WRITE);
                    bool vectorMode = src->supportsVectorMode() && dst->supportsVectorMode();
                    src->setVectorMode(vectorMode);
                    dst->setVectorMode(vectorMode);
                    size_t count = 0;
                    while (!src->end()) {
                        if (!emptyableToNonEmptyable) {
                            count += 1;
                        }
                        dst->setPosition(src->getPosition());
                        dst->writeItem(src->getItem());
                        ++(*src);
                    }
                    if (!vectorMode && !(src->getMode() & ChunkIterator::TILE_MODE) && !chunk.getArrayDesc().hasOverlap()) {
                        if (emptyableToNonEmptyable) {
                            count = outChunk.getNumberOfElements(false); // false = no overlap
                        }
                        outChunk.setCount(count);
                    }
                    dst->flush();
                }
            }
        } catch (...) {
            deleteChunk(outChunk);
            throw;
        }
        return outChunk;
    }

    int ConstItemIterator::getMode()
    {
        return iterationMode;
    }

     Value& ConstItemIterator::getItem()
    {
        return chunkIterator->getItem();
    }

    bool ConstItemIterator::isEmpty()
    {
        return chunkIterator->isEmpty();
    }

    ConstChunk const& ConstItemIterator::getChunk()
    {
        return chunkIterator->getChunk();
    }

    bool ConstItemIterator::end()
    {
        return !chunkIterator || chunkIterator->end();
    }

    void ConstItemIterator::operator ++()
    {
        ++(*chunkIterator);
        while (chunkIterator->end()) {
            chunkIterator.reset();
            ++(*arrayIterator);
            if (arrayIterator->end()) {
                return;
            }
            chunkIterator = arrayIterator->getChunk().getConstIterator(iterationMode);
        }
    }

    Coordinates const& ConstItemIterator::getPosition()
    {
        return chunkIterator->getPosition();
    }

    bool ConstItemIterator::setPosition(Coordinates const& pos)
    {
        if (!chunkIterator || !chunkIterator->setPosition(pos)) {
            chunkIterator.reset();
            if (arrayIterator->setPosition(pos)) {
                chunkIterator = arrayIterator->getChunk().getConstIterator(iterationMode);
                return chunkIterator->setPosition(pos);
            }
            return false;
        }
        return true;
    }

    void ConstItemIterator::reset()
    {
        chunkIterator.reset();
        arrayIterator->reset();
        if (!arrayIterator->end()) {
            chunkIterator = arrayIterator->getChunk().getConstIterator(iterationMode);
        }
    }

    ConstItemIterator::ConstItemIterator(Array const& array, AttributeID attrID, int mode)
    : arrayIterator(array.getConstIterator(attrID)),
      iterationMode(mode)
    {
        if (!arrayIterator->end()) {
            chunkIterator = arrayIterator->getChunk().getConstIterator(mode);
        }
    }


    static void dummyFunction(const Value** args, Value* res, void*) {}

    class UserDefinedRegistrator {
      public:
        UserDefinedRegistrator() {}

        void foo();
    };

    void UserDefinedRegistrator::foo()
    {
        REGISTER_FUNCTION(length, list_of(TID_STRING)(TID_STRING), TypeId(TID_INT64), dummyFunction);
        REGISTER_CONVERTER(string, char, TRUNCATE_CONVERSION_COST, dummyFunction);
        REGISTER_TYPE(decimal, 16);
    }

    UserDefinedRegistrator userDefinedRegistrator;

}
