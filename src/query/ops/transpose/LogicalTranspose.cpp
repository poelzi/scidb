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
 * LogicalTranspose.cpp
 *
 *  Created on: Mar 9, 2010
 */

#include "query/Operator.h"
#include "system/Exceptions.h"


namespace scidb
{

class LogicalTranspose : public LogicalOperator
{
public:
	LogicalTranspose(const std::string& logicalName, const std::string& alias):
	    LogicalOperator(logicalName, alias)
	{
        ADD_PARAM_INPUT()
 	}

    ArrayDesc inferSchema(std::vector< ArrayDesc> schemas, boost::shared_ptr< Query> query)
    {
        assert(schemas.size() == 1);
        assert(_parameters.size() == 0);

        ArrayDesc const& schema = schemas[0];

        Dimensions const& dims(schema.getDimensions());   
        Dimensions transDims(dims.size());

        for (size_t i = 0, n = dims.size(); i < n; i++)
        {
            transDims[n-i-1] = dims[i];
        }

        return ArrayDesc(schema.getName(), schema.getAttributes(), transDims);
	}

};

DECLARE_LOGICAL_OPERATOR_FACTORY(LogicalTranspose, "transpose")

} //namespace scidb