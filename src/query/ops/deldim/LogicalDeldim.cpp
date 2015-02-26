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
 * LogicalDeldim.cpp
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

class LogicalDeldim: public LogicalOperator
{
public:
    LogicalDeldim(const string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {
        ADD_PARAM_INPUT()
    }

    ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, boost::shared_ptr<Query> query)
    {
        assert(schemas.size() == 1);
        assert(_parameters.size() == 0);

        ArrayDesc const& srcArrayDesc = schemas[0];
        Dimensions const& srcDimensions = srcArrayDesc.getDimensions();
        
        if (srcDimensions.size() <= 1)
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_DELDIM_ERROR1);
        
        Dimensions dstDimensions(srcDimensions.size()-1);
        if (srcDimensions[0].getLength() != 1)
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_DELDIM_ERROR2);
        for (size_t i = 0, n = dstDimensions.size(); i < n; i++) {
            dstDimensions[i] = srcDimensions[i+1];
        }
        return ArrayDesc(srcArrayDesc.getName(), srcArrayDesc.getAttributes(), dstDimensions);
    }
};

DECLARE_LOGICAL_OPERATOR_FACTORY(LogicalDeldim, "deldim")


} //namespace
