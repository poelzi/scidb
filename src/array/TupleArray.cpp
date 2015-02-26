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
 * @file MemArray.cpp
 *
 * @brief Temporary (in-memory) array implementation
 *
 * @author Konstantin Knizhnik <knizhnik@garret.ru>
 */

#include "util/iqsort.h"
#include "array/TupleArray.h"
#include "system/Exceptions.h"
#include "query/Expression.h"
#include "query/FunctionDescription.h"
#include "query/Operator.h"

namespace scidb
{
    using namespace boost;
    using namespace std;

    //
    // Tuple comparator
    // 
    int TupleComparator::compare(Tuple const& t1, Tuple const& t2) 
    {
        for (size_t i = 0, n = _keys.size(); i < n; i++) {
            const int j = _keys[i].columnNo;
            //vector<Value> args(2, TypeLibrary::getType(_arrayDesc.getAttributes()[j].getType()));
            const Value* args[2];
            if (t1[j].isNull() || t2[j].isNull()) { 
                int diff = t1[j].isNull() ? t2[j].isNull() ? 0 : -1 : 1;
                return _keys[i].ascent ? diff : -diff;
            }
            args[0] = &t1[j];
            args[1] = &t2[j];
            //Value res(TypeLibrary::getType(TID_BOOL));
            Value res;
            
            _eqFunctions[i](&args[0], &res, NULL);
            if (!res.getBool()) {
                _leFunctions[i](&args[0], &res, NULL);
                return (res.getBool()) ? (_keys[i].ascent ? -1 : 1) : (_keys[i].ascent ? 1 : -1);
            }
        }
        return 0;
    }

    TupleComparator::TupleComparator(vector<Key> const& keys, const ArrayDesc& arrayDesc):
    _keys(keys), _arrayDesc(arrayDesc), _leFunctions(keys.size()), _eqFunctions(keys.size())
    {
        for (size_t i = 0; i < _keys.size(); i++) {
            vector<TypeId> argTypes(2, _arrayDesc.getAttributes()[_keys[i].columnNo].getType());
             FunctionDescription functionDesc;
            vector<FunctionPointer> converters;
            bool supportsVectorMode;
            if (!FunctionLibrary::getInstance()->findFunction("<", argTypes, functionDesc, converters, supportsVectorMode, false))
                throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_OPERATOR_NOT_FOUND) << "<" << argTypes[0];
            _leFunctions[i] = functionDesc.getFuncPtr();
            if (!FunctionLibrary::getInstance()->findFunction("=", argTypes, functionDesc, converters, supportsVectorMode, false))
                throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_OPERATOR_NOT_FOUND) << "=" << argTypes[0];
            _eqFunctions[i] = functionDesc.getFuncPtr();
        }
    }

    //
    // TupleArray 
    //     
    void TupleArray::sort(vector<Key> const& keys)
    { 
        TupleComparator comparator(keys, getArrayDesc());
        if (tuples.size() != 0) { 
            iqsort(&tuples[0], tuples.size(), comparator);
        }
    }
      
    ArrayDesc const& TupleArray::getArrayDesc() const
    {
        return desc;
    }

    boost::shared_ptr<ConstArrayIterator> TupleArray::getConstIterator(AttributeID attId) const
    {
        return boost::shared_ptr<ConstArrayIterator>(new TupleArrayIterator(*this, attId));
    }

    TupleArray::TupleArray(ArrayDesc const& schema, vector< boost::shared_ptr<Tuple> > const& data, Coordinate offset)
    : desc(schema), 
      start(schema.getDimensions()[0].getStart() + offset), 
      end(start + offset + schema.getDimensions()[0].getLength() - 1), 
      tuples(data), chunkSize(schema.getDimensions()[0].getChunkInterval())
    {
        if (Coordinate(start + tuples.size()) <= end) { 
            end = start + tuples.size() - 1;
        }
    }

    void TupleArray::truncate()
    {
        Dimensions newDims(1);
        DimensionDesc const& oldDim = desc.getDimensions()[0];
        newDims[0] = DimensionDesc(oldDim.getBaseName(),
                                   oldDim.getNamesAndAliases(),
                                   oldDim.getStart(), 
                                   oldDim.getStart(), 
                                   oldDim.getStart() + tuples.size() - 1, 
                                   oldDim.getStart() + tuples.size() - 1, 
                                   oldDim.getChunkInterval(), 
                                   0);
        desc = ArrayDesc(desc.getName(), desc.getAttributes(), newDims);
    }

    void TupleArray::append(boost::shared_ptr<Array> inputArray)
    {
        size_t nAttrs = desc.getAttributes().size();
        vector< boost::shared_ptr<ConstArrayIterator> > arrayIterators(nAttrs);
        for (size_t i = 0; i < nAttrs; i++) { 
            arrayIterators[i] = inputArray->getConstIterator(i);
        }
        append(arrayIterators, (size_t)-1);
    }

    void TupleArray::append(vector< boost::shared_ptr<ConstArrayIterator> > const& arrayIterators, size_t nChunks)
    {
        size_t nAttrs = desc.getAttributes().size();
        vector< boost::shared_ptr<ConstChunkIterator> > chunkIterators(nAttrs);
        while (nChunks-- != 0 && !arrayIterators[0]->end()) { 
            for (size_t i = 0; i < nAttrs; i++) { 
                chunkIterators[i] = arrayIterators[i]->getChunk().getConstIterator(ConstChunkIterator::IGNORE_EMPTY_CELLS|ConstChunkIterator::IGNORE_OVERLAPS);
            }
            while (!chunkIterators[0]->end()) { 
                if (!chunkIterators[0]->isEmpty()) {
                    Tuple& tuple = *new Tuple(nAttrs);
                    tuples.push_back(boost::shared_ptr<Tuple>(&tuple));
                    for (size_t i = 0; i < nAttrs; i++) {                         
                        tuple[i] = chunkIterators[i]->getItem();
                        ++(*chunkIterators[i]);
                    }                
                } else { 
                    for (size_t i = 0; i < nAttrs; i++) {                    
                        ++(*chunkIterators[i]);
                    }                                    
                }
            }
            for (size_t i = 0; i < nAttrs; i++) { 
                ++(*arrayIterators[i]);
            }                                
        }
    }

    void TupleArray::append(vector< boost::shared_ptr<ConstArrayIterator> > const& arrayIterators, size_t shift, size_t step)
    {
        size_t nAttrs = desc.getAttributes().size();
        vector< boost::shared_ptr<ConstChunkIterator> > chunkIterators(nAttrs);
        
        for (size_t j = shift; j != 0 && !arrayIterators[0]->end(); --j) {
            for (size_t i = 0; i < nAttrs; i++) { 
                ++(*arrayIterators[i]);
            }                                
        }
        while (!arrayIterators[0]->end()) { 
            for (size_t i = 0; i < nAttrs; i++) { 
                chunkIterators[i] = arrayIterators[i]->getChunk().getConstIterator(ConstChunkIterator::IGNORE_EMPTY_CELLS|ConstChunkIterator::IGNORE_OVERLAPS);
            }
            while (!chunkIterators[0]->end()) { 
                if (!chunkIterators[0]->isEmpty()) {
                    Tuple& tuple = *new Tuple(nAttrs);
                    tuples.push_back(boost::shared_ptr<Tuple>(&tuple));
                    for (size_t i = 0; i < nAttrs; i++) {                         
                        tuple[i] = chunkIterators[i]->getItem();
                        ++(*chunkIterators[i]);
                    }                
                } else { 
                    for (size_t i = 0; i < nAttrs; i++) {                    
                        ++(*chunkIterators[i]);
                    }                                    
                }
            }
            for (size_t j = step; j != 0 && !arrayIterators[0]->end(); --j) {
                for (size_t i = 0; i < nAttrs; i++) { 
                    ++(*arrayIterators[i]);
                }                                
            }
        }
    }

    //
    // TupleArrayIterator
    //
    ConstChunk const& TupleArrayIterator::getChunk()
    {
        if (!hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        chunk.lastPos = chunk.firstPos = currPos;
        if ((chunk.lastPos[0] += array.chunkSize - 1) > array.end) { 
            chunk.lastPos[0] = array.end;
        }
        return chunk;
    }

    bool TupleArrayIterator::end() 
    {
        return !hasCurrent;
    }

    void TupleArrayIterator::operator ++()
    {
        if (!hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        currPos[0] += array.chunkSize;            
        hasCurrent = currPos[0] <= array.end;
    }

    Coordinates const& TupleArrayIterator::getPosition() 
    {
        if (!hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        return currPos;
    }

    bool TupleArrayIterator::setPosition(Coordinates const& pos)
    {
        if (pos[0] >= array.start && pos[0] <= array.end) { 
            currPos[0] = pos[0] - ((pos[0] - array.start) % array.chunkSize);
            hasCurrent = true;
        } else { 
            hasCurrent = false;
        }
        return hasCurrent;
    }

    void TupleArrayIterator::reset()
    {
        currPos[0] = array.start;
        hasCurrent = currPos[0] <= array.end;
    }

    TupleArrayIterator::TupleArrayIterator(TupleArray const& arr, AttributeID att)
    : array(arr), attrID(att), chunk(arr, att), currPos(1)
    {
        reset();
    }

     TupleArray::TupleArray(ArrayDesc const& schema, boost::shared_ptr<Array> inputArray)
    : desc(schema), 
      start(schema.getDimensions()[0].getStart()), 
      end(schema.getDimensions()[0].getEndMax()), 
      chunkSize(schema.getDimensions()[0].getChunkInterval())
    {
        if (schema.getDimensions().size() != 1)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_MULTIDIMENSIONAL_ARRAY_NOT_ALLOWED);
        append(inputArray);
        if (start == MIN_COORDINATE || end == MAX_COORDINATE) { 
            start = 0;
            end = tuples.size()-1;
        } else if (Coordinate(start + tuples.size()) <= end) { 
            end = start + tuples.size() - 1;
        }
    }

    TupleArray::TupleArray(ArrayDesc const& schema, vector< boost::shared_ptr<ConstArrayIterator> > const& arrayIterators, size_t nChunks)
    : desc(schema), 
      start(schema.getDimensions()[0].getStart()), 
      end(schema.getDimensions()[0].getEndMax()), 
      chunkSize(schema.getDimensions()[0].getChunkInterval())
    {
        if (schema.getDimensions().size() != 1)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_MULTIDIMENSIONAL_ARRAY_NOT_ALLOWED);
        append(arrayIterators, nChunks);
        if (start == MIN_COORDINATE || end == MAX_COORDINATE) { 
            start = 0;
            end = tuples.size()-1;
        } else if (Coordinate(start + tuples.size()) <= end) { 
            end = start + tuples.size() - 1;
        }
    }

    TupleArray::TupleArray(ArrayDesc const& schema, vector< boost::shared_ptr<ConstArrayIterator> > const& arrayIterators, size_t shift, size_t step)
    : desc(schema), 
      start(schema.getDimensions()[0].getStart()), 
      end(schema.getDimensions()[0].getEndMax()), 
      chunkSize(schema.getDimensions()[0].getChunkInterval())
    {
        if (schema.getDimensions().size() != 1)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_MULTIDIMENSIONAL_ARRAY_NOT_ALLOWED);
        append(arrayIterators, shift, step);
        if (start == MIN_COORDINATE || end == MAX_COORDINATE) { 
            start = 0;
            end = tuples.size()-1;
        } else if (Coordinate(start + tuples.size()) <= end) { 
            end = start + tuples.size() - 1;
        }
    }

    //
    // Tuple chunk
    //

    const ArrayDesc& TupleChunk::getArrayDesc() const
    {
        return array.desc;
    }

    const AttributeDesc& TupleChunk::getAttributeDesc() const
    {
        return array.desc.getAttributes()[attrID];
    }

    int TupleChunk::getCompressionMethod() const
    {
        return getAttributeDesc().getDefaultCompressionMethod();
    }

    Coordinates const& TupleChunk::getFirstPosition(bool withOverlap) const
    {
        return firstPos;
    }

    Coordinates const& TupleChunk::getLastPosition(bool withOverlap) const
    {
        return lastPos;
    }

    boost::shared_ptr<ConstChunkIterator> TupleChunk::getConstIterator(int iterationMode) const
    {
        return boost::shared_ptr<ConstChunkIterator>(new TupleChunkIterator(*this, iterationMode));
    }

    TupleChunk::TupleChunk(TupleArray const& arr, AttributeID att)
    : array(arr), attrID(att), firstPos(1), lastPos(1)
    {
    }

    //
    // TupleChunkIterator
    //
    int TupleChunkIterator::getMode()
    {
        return mode;
    }

     Value& TupleChunkIterator::getItem()
    {
        if (i > last)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        return (*array.tuples[i])[attrID];
    }

    bool TupleChunkIterator::isEmpty()
    {
        if (i > last)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        return !array.tuples[i];
    }

    bool TupleChunkIterator::end()
    {
        return i > last;
    }
     
    inline bool TupleChunkIterator::isVisible() const
    {
        return !((mode & IGNORE_EMPTY_CELLS) && !array.tuples[i])
            && !((mode & IGNORE_NULL_VALUES) && array.tuples[i] && (*array.tuples[i])[attrID].isNull());
    }
    
    void TupleChunkIterator::operator ++()
    {
        if (i > last)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        while (++i <= last && !isVisible());
    }
    
    Coordinates const& TupleChunkIterator::getPosition()
    {
        if (i > last)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_CHUNK);
        currPos[0] = i + array.start;
        return currPos;
    }

    bool TupleChunkIterator::setPosition(Coordinates const& pos)
    {
        if (pos[0] < chunk.firstPos[0] || pos[0] > chunk.lastPos[0]) { 
            return false;
        }
        i = size_t(pos[0] - array.start);
        return isVisible();
    }
    
    void TupleChunkIterator::reset()
    {
        for (i = chunk.firstPos[0] - array.start; i <= last && !isVisible(); i++);
    }
     
    ConstChunk const& TupleChunkIterator::getChunk()
    {
        return chunk;
    }

    TupleChunkIterator::TupleChunkIterator(TupleChunk const& aChunk, int iterationMode)
    : chunk(aChunk),
      array(aChunk.array),
      attrID(aChunk.attrID),
      currPos(1),
      last(size_t(chunk.lastPos[0] - array.start)),
      mode(iterationMode)
    {
        reset();
    }
}

        
    
