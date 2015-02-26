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
 * PhysicalAdddim.cpp
 *
 *  Created on: Apr 20, 2010
 *      Author: Knizhnik
 */

#include "query/Operator.h"
#include "array/Metadata.h"
#include "AdddimArray.h"


using namespace std;
using namespace boost;

namespace scidb {

class PhysicalAdddim: public  PhysicalOperator
{
public:
	PhysicalAdddim(const string& logicalName, const string& physicalName, const Parameters& parameters, const ArrayDesc& schema):
	    PhysicalOperator(logicalName, physicalName, parameters, schema)
	{
	}

    virtual PhysicalBoundaries getOutputBoundaries(const std::vector<PhysicalBoundaries> & inputBoundaries,
                                                   const std::vector< ArrayDesc> & inputSchemas) const
    {
        PhysicalBoundaries input = inputBoundaries[0];
        Coordinates start, end;
        start.push_back(0);
        end.push_back(0);

        for (size_t i = 0; i<input.getStartCoords().size(); i++)
        {
            start.push_back(input.getStartCoords()[i]);
            end.push_back(input.getEndCoords()[i]);
        }
        return PhysicalBoundaries(start,end);
    }

	/***
	 * Adddim is a pipelined operator, hence it executes by returning an iterator-based array to the consumer
	 * that overrides the chunkiterator method.
	 */
	boost::shared_ptr<Array> execute(vector< boost::shared_ptr<Array> >& inputArrays, boost::shared_ptr<Query> query)
    {
		assert(inputArrays.size() == 1);
		return boost::shared_ptr<Array>(new AdddimArray(_schema, inputArrays[0]));
	 }
};
    
DECLARE_PHYSICAL_OPERATOR_FACTORY(PhysicalAdddim, "adddim", "physicalAdddim")

}  // namespace scidb