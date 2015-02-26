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
 * @file
 *
 * @brief Basic class for all optimizers
 *
 * @author knizhnik@garret.ru
 */
#include "query/optimizer/Optimizer.h"
#include "network/NetworkManager.h"

using namespace boost;

namespace scidb
{
    boost::shared_ptr< LogicalQueryPlanNode> Optimizer::logicalRewriteIfNeeded(const boost::shared_ptr<Query>& query,
                                                                               boost::shared_ptr< LogicalQueryPlanNode> node)
    {
        //rewrite load(array,'filename') into store(input(array,'filename'),array)

        //Note: this rewrite mechanism should be
        //  1. generic
        //  2. user-extensible

        //Note: optimizer also performs rewrites like "sum" -> "sum2(sum)" but we can't do these here because:
        //  1. they are physical; not logical
        //  2. they are recursive. We don't want logical rewrites to be recursive.

        OperatorLibrary *olib =  OperatorLibrary::getInstance();
        if (node->getLogicalOperator()->getLogicalName()=="load")
        {
            boost::shared_ptr< LogicalOperator> loadOperator = node->getLogicalOperator();

            LogicalOperator::Parameters loadParameters = loadOperator->getParameters();
            ArrayDesc outputSchema = loadOperator->getSchema();

            boost::shared_ptr< LogicalOperator> inputOperator = olib->createLogicalOperator("input");
            inputOperator->setParameters(loadParameters);
            inputOperator->setSchema(outputSchema);

            boost::shared_ptr< OperatorParam> paramArrayName = loadParameters[0];

            if ( query->getNodesCount() == 1) {
                boost::shared_ptr< LogicalOperator> storeOperator = olib->createLogicalOperator("store");
                storeOperator->addParameter(paramArrayName);
                
                std::vector< ArrayDesc> storeInputSchemas;
                storeInputSchemas.push_back(inputOperator->getSchema());
                
                storeOperator->setSchema(storeOperator->inferSchema(storeInputSchemas, query));

                boost::shared_ptr< LogicalQueryPlanNode> inputNode(
                    new  LogicalQueryPlanNode (node->getParsingContext(),
                                                     inputOperator));
                
                boost::shared_ptr< LogicalQueryPlanNode> storeNode(
                    new  LogicalQueryPlanNode (node->getParsingContext(),
                                                     storeOperator));

                //load node does not have any children. so the input node will also have none.
                assert(node->getChildren().size()==0);
                
                storeNode->addChild(inputNode);
                return storeNode;
            } else { 
                LogicalOperator::Parameters sgParams(3);    
                Value ival(TypeLibrary::getType(TID_INT32));
                ival.setInt32(psRoundRobin);
                sgParams[0] = boost::shared_ptr<OperatorParam>(
                    new OperatorParamLogicalExpression(node->getParsingContext(),
                                                       boost::shared_ptr<LogicalExpression>(new Constant(node->getParsingContext(), ival, TID_INT32)), 
                                                       TypeLibrary::getType(TID_INT32), true));
                ival.setInt32(-1);
                sgParams[1] = boost::shared_ptr<OperatorParam>(
                    new OperatorParamLogicalExpression(node->getParsingContext(), 
                                                       boost::shared_ptr<LogicalExpression>(new Constant(node->getParsingContext(), ival, TID_INT32)), 
                                                       TypeLibrary::getType(TID_INT32), true));
                sgParams[2] = paramArrayName;
                
                boost::shared_ptr< LogicalOperator> sgOperator = olib->createLogicalOperator("sg");
                sgOperator->setParameters(sgParams);

                std::vector< ArrayDesc> sgInputSchemas;
                sgInputSchemas.push_back(inputOperator->getSchema());
                
                sgOperator->setSchema(sgOperator->inferSchema(sgInputSchemas,query));

                boost::shared_ptr< LogicalQueryPlanNode> inputNode(
                    new  LogicalQueryPlanNode (node->getParsingContext(),
                                                     inputOperator));
                
                boost::shared_ptr< LogicalQueryPlanNode> sgNode(
                    new  LogicalQueryPlanNode (node->getParsingContext(),
                                                     sgOperator));

                //load node does not have any children. so the input node will also have none.
                assert(node->getChildren().size()==0);
                
                sgNode->addChild(inputNode);

                return sgNode;
            }
        }
        else if (node->getLogicalOperator()->getLogicalName()=="sum" ||
                 node->getLogicalOperator()->getLogicalName()=="avg" ||
                 node->getLogicalOperator()->getLogicalName()=="min" ||
                 node->getLogicalOperator()->getLogicalName()=="max" ||
                 node->getLogicalOperator()->getLogicalName()=="stdev" ||
                 node->getLogicalOperator()->getLogicalName()=="var" ||
                 node->getLogicalOperator()->getLogicalName()=="count")
        {
           boost::shared_ptr< LogicalOperator> oldStyleOperator = node->getLogicalOperator();
           boost::shared_ptr< LogicalOperator> aggOperator = olib->createLogicalOperator("aggregate");
           aggOperator->setSchema(oldStyleOperator->getSchema());
           LogicalOperator::Parameters oldStyleParams = oldStyleOperator->getParameters();

           if (node->getLogicalOperator()->getLogicalName()=="count")
           {
               shared_ptr<OperatorParam> asterisk (new OperatorParamAsterisk(node->getParsingContext()));

               shared_ptr<OperatorParam> aggCall ( new OperatorParamAggregateCall (node->getParsingContext(),
                                                                                   node->getLogicalOperator()->getLogicalName(),
                                                                                   asterisk,
                                                                                   ""));
               aggOperator->addParameter(aggCall);

           }
           else if (oldStyleParams.size() == 0)
           {
               ArrayDesc const& inputSchema = node->getChildren()[0]->getLogicalOperator()->getSchema();
               shared_ptr<OperatorParamReference> attRef ( new OperatorParamAttributeReference(node->getParsingContext(),
                                                                                               inputSchema.getName(),
                                                                                               inputSchema.getAttributes()[0].getName(),
                                                                                               true));
               attRef->setInputNo(0);
               attRef->setObjectNo(0);

               shared_ptr<OperatorParam> aggCall ( new OperatorParamAggregateCall (node->getParsingContext(),
                                                                                   node->getLogicalOperator()->getLogicalName(),
                                                                                   attRef,
                                                                                   ""));
               aggOperator->addParameter(aggCall);
           }

           for (size_t i =0; i<oldStyleParams.size(); i++)
           {
               if (oldStyleParams[i]->getParamType() == PARAM_ATTRIBUTE_REF)
               {
                   shared_ptr<OperatorParam> aggCall ( new OperatorParamAggregateCall (oldStyleParams[i]->getParsingContext(),
                                                                                       node->getLogicalOperator()->getLogicalName(),
                                                                                       oldStyleParams[i],
                                                                                       ""));
                   aggOperator->addParameter(aggCall);
               }
               else if (oldStyleParams[i]->getParamType() == PARAM_DIMENSION_REF)
               {
                   aggOperator->addParameter(oldStyleParams[i]);
               }
           }

           boost::shared_ptr< LogicalQueryPlanNode> aggNode( new  LogicalQueryPlanNode (node->getParsingContext(), aggOperator));
           assert(node->getChildren().size() == 1);
           aggNode->addChild(node->getChildren()[0]);
           return aggNode;
        }
        else
        {
           return node;
        }
    }
}
