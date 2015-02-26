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
 * PhysicalMultiply.cpp
 *
 *  Created on: Mar 9, 2010
 */
#include "query/Operator.h"
#include "array/Metadata.h"
#include "array/MemArray.h"
#include "query/ops/multiply/MultiplyArray.h"

namespace scidb
{

class PhysicalMultiply: public  PhysicalOperator
{
public:
	PhysicalMultiply(const std::string& logicalName, const std::string& physicalName, const Parameters& parameters, const ArrayDesc& schema):
	    PhysicalOperator(logicalName, physicalName, parameters, schema)
	{
	}

    virtual bool isDistributionPreserving(const std::vector<ArrayDesc> & inputSchemas) const
    {
        return false;
    }

    virtual ArrayDistribution getOutputDistribution(const std::vector<ArrayDistribution> & inputDistributions,
                                                 const std::vector< ArrayDesc> & inputSchemas) const
    {
        DimensionDesc const& d1 = inputSchemas[0].getDimensions()[0];
        DimensionDesc const& d2 = inputSchemas[1].getDimensions()[1];
        PartitioningSchema ps = d1.getLength() <= d2.getLength() ? psByCol : psByRow;
        return ArrayDistribution(ps);
    }

	/***
	 */
	boost::shared_ptr< Array> execute(std::vector< boost::shared_ptr< Array> >& inputArrays,
            boost::shared_ptr<Query> query)
	{
        return boost::shared_ptr<Array>(new MultiplyArray(_schema, inputArrays[0], inputArrays[1], query));
	}
};

DECLARE_PHYSICAL_OPERATOR_FACTORY(PhysicalMultiply, "multiply", "PhysicalMultiply")

} //namespace scidb
