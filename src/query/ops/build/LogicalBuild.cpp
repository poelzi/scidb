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
 * LogicalBuild.cpp
 *
 *  Created on: Apr 20, 2010
 *      Author: Knizhnik
 */

#include "query/Operator.h"
#include "system/SystemCatalog.h"
#include "system/Exceptions.h"

using namespace std;

namespace scidb
{

class LogicalBuild: public LogicalOperator
{
public:
    LogicalBuild(const string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {
    	ADD_PARAM_SCHEMA()
    	ADD_PARAM_EXPRESSION("void")
    }

    ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, boost::shared_ptr<Query> query)
    {
        assert(schemas.size() == 0);
        assert(_parameters.size() == 2);

        ArrayDesc desc = ((boost::shared_ptr<OperatorParamSchema>&)_parameters[0])->getSchema();
        if (desc.getAttributes(true).size() != 1)
            throw USER_QUERY_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_BUILD_ERROR2,
                                       _parameters[0]->getParsingContext());

        if (desc.getName().size() == 0)
        {
            desc.setName("build");
        }

        // Check dimensions
        Dimensions const& dims = desc.getDimensions();
        for (size_t i = 0, n = dims.size();  i < n; i++)
        {
            if (dims[i].getLength() == INFINITE_LENGTH)
                throw USER_QUERY_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_BUILD_ERROR3,
                                           _parameters[0]->getParsingContext());
        }
        return desc;
    }
};

DECLARE_LOGICAL_OPERATOR_FACTORY(LogicalBuild, "build")

} //namespace