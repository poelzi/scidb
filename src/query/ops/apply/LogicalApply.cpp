
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
 * LogicalApply.cpp
 *
 *  Created on: Apr 11, 2010
 *      Author: Knizhnik
 */

#include <boost/shared_ptr.hpp>

#include "query/Operator.h"
#include "system/Exceptions.h"
#include "query/TypeSystem.h"

namespace scidb {

using namespace std;

class Apply: public  LogicalOperator
{
public:
    Apply(const std::string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {
        _properties.tile = true;
        ADD_PARAM_INPUT()
        ADD_PARAM_OUT_ATTRIBUTE_NAME("void")//0
        ADD_PARAM_EXPRESSION("void")        //1
        ADD_PARAM_VARIES()
    }

    virtual bool compileParamInTileMode(size_t paramNo) { 
        return (paramNo % 2) == 1;
    }

    std::vector<boost::shared_ptr<OperatorParamPlaceholder> > nextVaryParamPlaceholder(const std::vector< ArrayDesc> &schemas)
    {
        std::vector<boost::shared_ptr<OperatorParamPlaceholder> > res;
        res.push_back(END_OF_VARIES_PARAMS());
        if (_parameters.size() % 2 == 0)
        {   res.push_back(PARAM_OUT_ATTRIBUTE_NAME("void")); }
        else
        {   res.push_back(PARAM_EXPRESSION("void")); }
        return res;
    }

    ArrayDesc inferSchema(std::vector< ArrayDesc> schemas, boost::shared_ptr< Query> query)
    {
        assert(schemas.size() == 1);
        assert(_parameters[0]->getParamType() == PARAM_ATTRIBUTE_REF);
        assert(_parameters[1]->getParamType() == PARAM_LOGICAL_EXPRESSION);

        if ( _parameters.size() % 2 != 0 )
        {
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_WRONG_OPERATOR_ARGUMENTS_COUNT2) << "apply";
        }

        Attributes outAttrs;
        AttributeID nextAttrId =0;

        for (size_t i=0; i<schemas[0].getAttributes().size(); i++)
        {
            AttributeDesc const& attr = schemas[0].getAttributes()[i];
            if(attr.getType()!=TID_INDICATOR)
            {
                outAttrs.push_back( AttributeDesc(nextAttrId++,
                                                  attr.getName(),
                                                  attr.getType(),
                                                  attr.getFlags(),
                                                  attr.getDefaultCompressionMethod(),
                                                  attr.getAliases(),
                                                  attr.getReserve(),
                                                  &attr.getDefaultValue(),
                                                  attr.getDefaultValueExpr(),
                                                  attr.getComment(),
                                                  attr.getVarSize()));
            }
        }

        size_t k;
        for (k=0; k<_parameters.size(); k+=2)
        {
            const string &attributeName = ((boost::shared_ptr<OperatorParamReference>&)_parameters[k])->getObjectName();
            Expression expr;
            expr.compile(((boost::shared_ptr<OperatorParamLogicalExpression>&)_parameters[k+1])->getExpression(), query, _properties.tile, TID_VOID, schemas);
            int flags = 0;
            if (expr.isNullable())
            {
                flags = (int)AttributeDesc::IS_NULLABLE;
            }

            for (size_t j = 0; j < nextAttrId; j++) {
                AttributeDesc const& attr = outAttrs[j];
                if (attr.getName() ==  attributeName)
                {
                    throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_DUPLICATE_ATTRIBUTE_NAME) << attributeName;
                }
            }

            outAttrs.push_back(AttributeDesc(nextAttrId++,
                                             attributeName,
                                             expr.getType(),
                                             flags,
                                             0));
        }

        if(schemas[0].getEmptyBitmapAttribute())
        {
            AttributeDesc const* emptyTag = schemas[0].getEmptyBitmapAttribute();
            for (size_t j = 0; j < nextAttrId; j++)
            {
                AttributeDesc const& attr = outAttrs[j];
                if (attr.getName() ==  emptyTag->getName())
                {
                    throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_DUPLICATE_ATTRIBUTE_NAME) << attr.getName();
                }
            }

            outAttrs.push_back( AttributeDesc(nextAttrId,
                                              emptyTag->getName(),
                                              emptyTag->getType(),
                                              emptyTag->getFlags(),
                                              emptyTag->getDefaultCompressionMethod(),
                                              emptyTag->getAliases(),
                                              emptyTag->getReserve(),
                                              &emptyTag->getDefaultValue(),
                                              emptyTag->getDefaultValueExpr(),
                                              emptyTag->getComment(),
                                              emptyTag->getVarSize()));
        }

        return ArrayDesc(schemas[0].getName(), outAttrs, schemas[0].getDimensions());
    }
};

DECLARE_LOGICAL_OPERATOR_FACTORY(Apply, "apply")


}  // namespace scidb
